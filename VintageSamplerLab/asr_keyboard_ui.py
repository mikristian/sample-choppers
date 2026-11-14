"""Playable 61-key ASR-inspired multisample software keyboard (not firmware emulation)."""

from pathlib import Path
import subprocess
import tempfile
import tkinter as tk
from tkinter import filedialog,messagebox,ttk

from asr_keyzones import KeyboardProgram,KeyZone
from sampler_engine import probe


NOTES=range(36,97)
WHITE={0,2,4,5,7,9,11}


class ASRStyleKeyboardUI:
    def __init__(self,master=None):
        self.root=tk.Toplevel(master) if master is not None else tk.Tk()
        self.root.title('ASR-style Multisample Keyboard · Original prototype')
        self.root.geometry('1050x500')
        self.program=KeyboardProgram()
        self.temp=tempfile.TemporaryDirectory(prefix='asr-style-keyboard-')
        self.sample=tk.StringVar(value='No WAV loaded')
        self.root_note=tk.IntVar(value=60)
        self.low=tk.IntVar(value=36)
        self.high=tk.IntVar(value=96)
        self.velocity=tk.IntVar(value=100)
        self.status=tk.StringVar(value='Load a 16-bit WAV as a keyzone. Play 61 keys, MIDI notes 36–96.')
        self.proc=[]
        self.make_ui()
        self.root.protocol('WM_DELETE_WINDOW',self.close)

    def make_ui(self):
        head=ttk.Frame(self.root,padding=12);head.pack(fill='x')
        ttk.Label(head,text='ASR-STYLE MULTISAMPLE KEYBOARD',font=('Helvetica',19,'bold')).pack(anchor='w')
        ttk.Label(head,text='Original keyzone and velocity sampler · Not an Ensoniq firmware or effects emulation').pack(anchor='w')
        bar=ttk.Frame(self.root,padding=(12,8,12,4));bar.pack(fill='x')
        ttk.Button(bar,text='Add WAV zone',command=self.add_zone).pack(side='left')
        ttk.Label(bar,text='Root MIDI').pack(side='left',padx=(14,4))
        ttk.Entry(bar,textvariable=self.root_note,width=5).pack(side='left')
        ttk.Label(bar,text='Low').pack(side='left',padx=(14,4))
        ttk.Entry(bar,textvariable=self.low,width=5).pack(side='left')
        ttk.Label(bar,text='High').pack(side='left',padx=(14,4))
        ttk.Entry(bar,textvariable=self.high,width=5).pack(side='left')
        ttk.Label(bar,text='Velocity').pack(side='left',padx=(14,4))
        ttk.Scale(bar,variable=self.velocity,from_=1,to=127,length=140).pack(side='left')
        ttk.Button(bar,text='Save program JSON',command=self.save_program).pack(side='right',padx=5)
        ttk.Button(bar,text='Load program JSON',command=self.load_program).pack(side='right',padx=5)
        ttk.Label(self.root,textvariable=self.sample,wraplength=900).pack(anchor='w',padx=18,pady=4)

        keys=ttk.LabelFrame(self.root,text='Five-octave keybed · click keys to play',padding=(8,12));keys.pack(fill='both',expand=True,padx=12,pady=10)
        canvas=tk.Canvas(keys,height=200,background='#222222')
        canvas.pack(fill='both',expand=True)
        self.canvas=canvas
        white_width=25
        white_position={}
        count=0
        for midi in NOTES:
            if midi%12 in WHITE:
                white_position[midi]=count
                count+=1
        for midi in NOTES:
            if midi in white_position:
                idx=white_position[midi];x=idx*white_width
                fill='#f8f8f8' if midi%12 else '#e1e9ef'
                key=canvas.create_rectangle(x,10,x+white_width-1,185,fill=fill,outline='#333333')
                canvas.tag_bind(key,'<Button-1>',lambda event,note=midi:self.play_note(note))
                if midi%12==0:
                    label=canvas.create_text(x+12,166,text=str(midi),font=('Helvetica',9),fill='#111111')
                    canvas.tag_bind(label,'<Button-1>',lambda event,note=midi:self.play_note(note))
        for midi in NOTES:
            if midi%12 not in WHITE:
                # The most recent white key is immediately to its left.
                lower=midi-1
                while lower not in white_position:lower-=1
                x=(white_position[lower]+1)*white_width-8
                k=canvas.create_rectangle(x,10,x+16,118,fill='#1a1b20',outline='#111111')
                canvas.tag_bind(k,'<Button-1>',lambda event,note=midi:self.play_note(note))
        canvas.configure(scrollregion=(0,0,count*white_width,205))

        foot=ttk.Frame(self.root,padding=12);foot.pack(fill='x')
        ttk.Label(foot,textvariable=self.status,wraplength=1000).pack(anchor='w')
        ttk.Label(foot,text='Notes play with varispeed pitching and a short attack/release. Advanced Ensoniq ESP effects and original OS are not implemented.',
                  foreground='#777777').pack(anchor='w')

    def add_zone(self):
        filename=filedialog.askopenfilename(title='Choose original 16-bit mono/stereo PCM WAV',filetypes=[('WAV audio','*.wav')])
        if not filename:return
        try:
            probe(filename)
            zone=KeyZone(filename,self.root_note.get(),self.low.get(),self.high.get(),
                         duration_seconds=3.0)
        except Exception as exc:messagebox.showerror('Invalid sample/keyzone',str(exc));return
        self.program.zones.append(zone)
        self.sample.set(f'{len(self.program.zones)} loaded zone(s). Latest: {Path(filename).name} · root note {zone.root_note}.')
        self.status.set('Click a piano key to hear its varispeed sample mapping.')

    def play_note(self,note):
        try:
            velocity=round(self.velocity.get())
            out=Path(self.temp.name)/f'note-{note}-{velocity}.wav'
            self.program.export_note(out,note,velocity)
            self.proc=[p for p in self.proc if p.poll() is None]
            if len(self.proc)>=10:
                self.proc[0].terminate();self.proc.pop(0)
            self.proc.append(subprocess.Popen(['/usr/bin/afplay',str(out)],
                                              stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
            self.status.set(f'MIDI note {note} played at velocity {velocity} from {len(self.program.zones)} keyzone(s).')
        except Exception as exc:messagebox.showerror('Cannot render note',str(exc))

    def save_program(self):
        if not self.program.zones:
            messagebox.showinfo('No zones yet','First add a source WAV zone.');return
        p=filedialog.asksaveasfilename(title='Save keyzone program',defaultextension='.json',filetypes=[('JSON','*.json')])
        if not p:return
        try:self.program.save_program(p);self.status.set('Saved program configuration to '+p)
        except Exception as exc:messagebox.showerror('Cannot save program',str(exc))

    def load_program(self):
        p=filedialog.askopenfilename(title='Load keyzone program',filetypes=[('JSON','*.json')])
        if not p:return
        try:
            prog=KeyboardProgram.load_program(p)
            for zone in prog.zones:
                if not Path(zone.source).is_file():raise ValueError('Missing referenced WAV: '+zone.source)
            self.program=prog
            self.sample.set(f'Loaded {len(prog.zones)} keyzones from '+Path(p).name)
        except Exception as exc:messagebox.showerror('Cannot load program',str(exc))

    def close(self):
        for proc in self.proc:
            if proc.poll() is None:proc.terminate()
        self.temp.cleanup()
        self.root.destroy()

    def run(self):
        self.root.mainloop()

if __name__=='__main__':ASRStyleKeyboardUI().run()
