"""Playable 16-pad, step-sequencer and sample-chop desktop prototype.

Clean-room original UI/workflow; no code or visual assets from Native Instruments,
Akai or Ensoniq. AU/VST3 hosting isn't implemented; MIDI files import into DAWs.
"""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from pad_sequencer import PAD_KEYS, PadPattern
from sampler_engine import PROFILES, export_kit, probe
from asr_keyboard_ui import ASRStyleKeyboardUI


class LivePadStudio:
    def __init__(self):
        self.root=tk.Tk()
        self.root.title('Vintage Sampler Lab · Live 16 Pad Studio')
        self.root.geometry('940x660')
        self.root.minsize(760,580)
        self.preview_dir=tempfile.TemporaryDirectory(prefix='vintage-sampler-live-')
        self.processes=[]
        self.source=None
        self.loaded_kit=None
        self.prepared_key=None
        self.profile=tk.StringVar(value='MPC2000XL-inspired')
        self.start=tk.StringVar(value='0')
        self.length=tk.StringVar(value='8')
        self.pitch=tk.DoubleVar(value=0)
        self.bpm=tk.StringVar(value='132')
        self.bars=tk.StringVar(value='2')
        self.status=tk.StringVar(value='Load an existing 16-pad kit, or load a 16-bit WAV and slice it.')
        self.pattern=PadPattern(132,2)
        self.recording=False
        self.record_start=None
        self.playing=False
        self.loop_start=None
        self.next_step=0
        self.step_window=None
        self.step_vars={}
        self.selected_bar=tk.IntVar(value=1)
        self.buttons=[]
        self.configure_ui()
        self.root.bind('<KeyPress>',self.handle_key)
        self.root.protocol('WM_DELETE_WINDOW',self.close)

    def configure_ui(self):
        top=ttk.Frame(self.root,padding=14)
        top.pack(fill='x')
        ttk.Label(top,text='VINTAGE SAMPLER LAB',font=('Helvetica',21,'bold')).pack(anchor='w')
        ttk.Label(top,text='MPC-style chopping · Maschine-style playable pads · 16th-note sequencer · editable MIDI export').pack(anchor='w')

        sourcebar=ttk.Frame(self.root,padding=(14,0,14,5))
        sourcebar.pack(fill='x')
        ttk.Button(sourcebar,text='Load WAV',command=self.choose_wav).pack(side='left',padx=4)
        ttk.Button(sourcebar,text='Load 16-pad kit',command=self.choose_kit).pack(side='left',padx=4)
        ttk.Label(sourcebar,text='Profile').pack(side='left',padx=(12,4))
        ttk.Combobox(sourcebar,textvariable=self.profile,values=list(PROFILES),width=20,state='readonly').pack(side='left')
        ttk.Label(sourcebar,text='Start(s)').pack(side='left',padx=(10,2))
        ttk.Entry(sourcebar,textvariable=self.start,width=6).pack(side='left')
        ttk.Label(sourcebar,text='Length(s)').pack(side='left',padx=(10,2))
        ttk.Entry(sourcebar,textvariable=self.length,width=6).pack(side='left')

        options=ttk.Frame(self.root,padding=(14,2,14,4))
        options.pack(fill='x')
        ttk.Label(options,text='Varispeed semitones').pack(side='left',padx=5)
        ttk.Scale(options,from_=-12,to=12,variable=self.pitch,orient='horizontal',length=210).pack(side='left',padx=8)
        ttk.Label(options,text='BPM').pack(side='left',padx=(18,4))
        ttk.Entry(options,textvariable=self.bpm,width=7).pack(side='left')
        ttk.Label(options,text='Bars').pack(side='left',padx=(12,4))
        ttk.Combobox(options,textvariable=self.bars,values=('1','2','4','8'),width=4,state='readonly').pack(side='left')

        pads=ttk.LabelFrame(self.root,text='Live 4 × 4 sample pads    Keyboard: 1–4  /  Q–R  /  A–F  /  Z–V',padding=14)
        pads.pack(fill='both',expand=True,padx=14,pady=6)
        for pad,key in enumerate(PAD_KEYS):
            b=ttk.Button(pads,text=f'PAD {pad+1:02d}    [{key.upper()}]',
                         command=lambda pad=pad:self.hit(pad))
            b.grid(row=pad//4,column=pad%4,padx=6,pady=6,sticky='nsew')
            self.buttons.append(b)
        for i in range(4):
            pads.columnconfigure(i,weight=1)
            pads.rowconfigure(i,weight=1)

        transport=ttk.Frame(self.root,padding=(14,6,14,2))
        transport.pack(fill='x')
        ttk.Button(transport,text='Record live chops',command=self.toggle_record).pack(side='left',padx=4)
        ttk.Button(transport,text='Play loop',command=self.toggle_playback).pack(side='left',padx=4)
        ttk.Button(transport,text='Stop',command=self.stop).pack(side='left',padx=4)
        ttk.Button(transport,text='Edit 16-step grid',command=self.open_step_editor).pack(side='left',padx=4)
        ttk.Button(transport,text='Export pattern MIDI',command=self.export_midi).pack(side='left',padx=4)
        ttk.Button(transport,text='Export WAV pads',command=self.export_pads).pack(side='left',padx=4)
        ttk.Button(transport,text='ASR-style 61-key keyboard',command=self.open_keyboard).pack(side='left',padx=4)
        ttk.Button(transport,text='Clear pattern',command=self.clear_pattern).pack(side='left',padx=4)

        footer=ttk.Frame(self.root,padding=(18,8))
        footer.pack(fill='x')
        ttk.Label(footer,textvariable=self.status,wraplength=890).pack(anchor='w')
        ttk.Label(footer,text='Prototype: afplay previews are not low-latency calibrated; exports are the precise arrangement source for DAWs.',
                  foreground='#777777').pack(anchor='w')

    def handle_key(self,event):
        if isinstance(event.widget,(tk.Entry,ttk.Entry,ttk.Combobox)):
            return
        try: pad=PAD_KEYS.index((event.char or '').lower())
        except ValueError: return
        self.hit(pad)

    def choose_wav(self):
        p=filedialog.askopenfilename(title='Select uncompressed 16-bit WAV',filetypes=[('WAV audio','*.wav')])
        if not p:return
        try:info=probe(p)
        except Exception as exc:messagebox.showerror('Unsupported WAV',str(exc));return
        self.source=Path(p)
        self.loaded_kit=None
        self.prepared_key=None
        self.status.set(f'Loaded {self.source.name}: {info["duration"]} seconds · {info["rate"]} Hz. Press a pad to prepare 16 chops.')

    def choose_kit(self):
        p=filedialog.askopenfilename(title='Choose kit.json from existing 16-pad kit',filetypes=[('Sampler kit','kit.json'),('JSON','*.json')])
        if not p:return
        try:
            payload=json.loads(Path(p).read_text())
            folder=Path(p).parent
            if payload.get('format')!='VintageSamplerLab-Kit-1' or len(payload.get('pads',[]))!=16:
                raise ValueError('This is not a Vintage Sampler Lab 16-pad kit')
            if not all((folder/f'pad_{n:02d}.wav').is_file() for n in range(1,17)):
                raise ValueError('The kit is missing one or more WAV pads')
        except Exception as exc:messagebox.showerror('Invalid kit',str(exc));return
        self.source=None
        self.loaded_kit=folder
        self.prepared_key=None
        self.status.set(f'Loaded kit {folder.name}; press 1–4 / Q–R / A–F / Z–V to play. MIDI notes 36–51.')

    def current_pattern(self):
        try:
            bpm=float(self.bpm.get());bars=int(self.bars.get())
            if bars!=self.pattern.bars:
                hits={(pad,step) for pad,step in self.pattern.hits if step < bars*16}
                self.pattern=PadPattern(bpm,bars,hits)
            elif bpm!=self.pattern.bpm:
                self.pattern=PadPattern(bpm,bars,set(self.pattern.hits))
        except (ValueError,OverflowError) as exc:
            raise ValueError('BPM must be 40–240; bars must be 1,2,4 or 8') from exc
        return self.pattern

    def ready_kit(self):
        if self.loaded_kit:
            return self.loaded_kit
        if self.source is None:
            raise ValueError('Load a WAV or an existing pad kit first')
        key=(str(self.source),self.source.stat().st_mtime_ns,self.source.stat().st_size,
             self.profile.get(),self.start.get(),self.length.get(),round(self.pitch.get()))
        root=Path(self.preview_dir.name)
        if key!=self.prepared_key or not (root/'pad_16.wav').is_file():
            export_kit(self.source,root,self.profile.get(),round(self.pitch.get()),
                       float(self.start.get()),float(self.length.get()),16)
            self.prepared_key=key
        return root

    def play_audio(self,pad):
        try:folder=self.ready_kit()
        except Exception as exc:messagebox.showerror('Cannot play pad',str(exc));return False
        self.processes=[p for p in self.processes if p.poll() is None]
        if len(self.processes)>=12:
            self.processes[0].terminate();self.processes.pop(0)
        try:
            proc=subprocess.Popen(['/usr/bin/afplay',str(folder/f'pad_{pad+1:02d}.wav')],
                                  stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.processes.append(proc)
        except OSError as exc:
            self.status.set('Audio preview unavailable: '+str(exc));return False
        return True

    def hit(self,pad):
        if not self.play_audio(pad):return
        if self.recording and self.record_start is not None:
            pattern=self.current_pattern()
            step=pattern.record_at(pad,time.monotonic()-self.record_start)
            self.status.set(f'RECORDING · pad {pad+1} → step {step+1} of {pattern.steps} · {pattern.bpm:g} BPM')
            self.refresh_steps()
        else:
            self.status.set(f'Playing pad {pad+1:02d} [{PAD_KEYS[pad].upper()}] · {self.profile.get() if self.source else "loaded kit"}')

    def toggle_record(self):
        try:pattern=self.current_pattern()
        except ValueError as exc:messagebox.showerror('Invalid tempo',str(exc));return
        self.recording=not self.recording
        if self.recording:
            self.record_start=self.loop_start if self.playing else time.monotonic()
            self.status.set(f'RECORD ON — play pads using keys or clicks. Hits quantize to 1/16 at {pattern.bpm:g} BPM.')
        else:self.status.set(f'RECORD OFF — {len(pattern.hits)} hits retained. Export MIDI to Ableton / FL Studio.')

    def toggle_playback(self):
        if self.playing:
            self.stop();return
        try:pattern=self.current_pattern();self.ready_kit()
        except Exception as exc:messagebox.showerror('Cannot start playback',str(exc));return
        self.playing=True
        self.loop_start=time.monotonic()
        self.next_step=0
        if self.recording:self.record_start=self.loop_start
        self._tick()

    def _tick(self):
        if not self.playing:return
        pattern=self.pattern
        now=time.monotonic()
        # Catch up after stalled UI, without allowing infinite catch-up loops.
        handled=0
        while self.playing and now >= self.loop_start+self.next_step*pattern.step_seconds and handled<8:
            step=self.next_step % pattern.steps
            for pad in sorted(pad for pad,s in pattern.hits if s==step):
                self.play_audio(pad)
            self.next_step+=1
            handled+=1
        self.status.set(f'PLAYING · step {(self.next_step-1)%pattern.steps+1}/{pattern.steps} · {pattern.bpm:g} BPM · {len(pattern.hits)} hits')
        self.root.after(8,self._tick)

    def stop(self):
        self.playing=False
        self.recording=False
        self.loop_start=None
        self.record_start=None
        for proc in self.processes:
            if proc.poll() is None:proc.terminate()
        self.processes.clear()
        self.status.set(f'Stopped. Pattern contains {len(self.pattern.hits)} hits.')

    def clear_pattern(self):
        self.stop()
        self.pattern.hits.clear()
        self.refresh_steps()
        self.status.set('Cleared sequencer. Original samples untouched.')

    def open_keyboard(self):
        ASRStyleKeyboardUI(self.root)

    def export_midi(self):
        try:pattern=self.current_pattern()
        except ValueError as exc:messagebox.showerror('Invalid tempo',str(exc));return
        p=filedialog.asksaveasfilename(title='Save editable MIDI pattern',defaultextension='.mid',filetypes=[('MIDI File','*.mid')])
        if not p:return
        try:
            target=pattern.write_midi(p)
            pattern.write_json(target.with_suffix('.json'))
            self.status.set(f'Exported {len(pattern.hits)} quantized hits to {target.name} plus pattern JSON.')
        except Exception as exc:messagebox.showerror('MIDI export failed',str(exc))

    def export_pads(self):
        p=filedialog.askdirectory(title='Export pad WAVs to folder')
        if not p:return
        try:
            kit=self.ready_kit()
            dest=Path(p)
            if dest.resolve()==kit.resolve():
                raise ValueError('Select a different output directory to preserve the source kit')
            for index in range(1,17):
                shutil.copy2(kit/f'pad_{index:02d}.wav',dest/f'pad_{index:02d}.wav')
            shutil.copy2(kit/'kit.json',dest/'kit.json')
            self.status.set(f'Exported 16 sample WAV pads and kit.json to {dest}')
        except Exception as exc:messagebox.showerror('WAV export failed',str(exc))

    def open_step_editor(self):
        try:pattern=self.current_pattern()
        except ValueError as exc:messagebox.showerror('Invalid tempo',str(exc));return
        if self.step_window is not None and self.step_window.winfo_exists():
            self.step_window.lift();return
        window=tk.Toplevel(self.root)
        window.title('16 × 16 step sequencer · One bar per page')
        window.geometry('1120x570')
        self.step_window=window
        head=ttk.Frame(window,padding=10);head.pack(fill='x')
        ttk.Label(head,text='Click squares to program 16 pads × 16 steps. A bar is 16 sixteenth-notes.').pack(side='left')
        ttk.Label(head,text='Bar').pack(side='left',padx=(25,5))
        self.selected_bar.set(1)
        box=ttk.Combobox(head,values=[str(i) for i in range(1,pattern.bars+1)],width=4,state='readonly')
        box.set('1');box.pack(side='left')
        box.bind('<<ComboboxSelected>>',lambda e:(self.selected_bar.set(int(box.get())),self.refresh_steps()))
        grid=tk.Frame(window)
        grid.pack(fill='both',expand=True,padx=10,pady=5)
        self.step_vars={}
        ttk.Label(grid,text='Pad / step').grid(row=0,column=0)
        for col in range(16):
            ttk.Label(grid,text=str(col+1)).grid(row=0,column=col+1,padx=3)
        for pad in range(16):
            ttk.Label(grid,text=f'{pad+1:02d} [{PAD_KEYS[pad].upper()}]').grid(row=pad+1,column=0,sticky='w')
            for step in range(16):
                var=tk.BooleanVar()
                self.step_vars[(pad,step)]=var
                cb=ttk.Checkbutton(grid,variable=var,
                                   command=lambda pad=pad,step=step,var=var:self.set_step(pad,step,var.get()))
                cb.grid(row=pad+1,column=step+1,padx=3,pady=2)
        ttk.Label(window,text='Hit Export pattern MIDI in the main window to use the beat in Ableton or FL Studio.').pack(pady=5)
        window.protocol('WM_DELETE_WINDOW',self.close_steps)
        self.refresh_steps()

    def set_step(self,pad,step,on):
        try:pattern=self.current_pattern()
        except ValueError:return
        absolute=(self.selected_bar.get()-1)*16+step
        if absolute<pattern.steps:
            pattern.set_hit(pad,absolute,on)
            self.status.set(f'Pattern now has {len(pattern.hits)} hits · {pattern.bars} bar(s).')

    def refresh_steps(self):
        if not self.step_window or not self.step_window.winfo_exists():return
        bar=self.selected_bar.get()
        for (pad,step),var in self.step_vars.items():
            var.set((pad,(bar-1)*16+step) in self.pattern.hits)

    def close_steps(self):
        if self.step_window is not None:self.step_window.destroy()
        self.step_window=None
        self.step_vars={}

    def close(self):
        self.stop()
        self.preview_dir.cleanup()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__=='__main__':
    LivePadStudio().run()
