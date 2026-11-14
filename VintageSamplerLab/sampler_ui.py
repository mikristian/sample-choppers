"""16-pad Mac GUI around the offline clean-room sample engine."""

from pathlib import Path
import subprocess
import sys
import tempfile
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from sampler_engine import PROFILES, export_kit, probe


class SamplerApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title('Vintage Sampler Lab · Prototype')
        self.root.geometry('690x540')
        self.sample = None
        self.preview_dir = tempfile.TemporaryDirectory(prefix='vintage-pad-preview-')
        self.preview_process = None
        self.profile = tk.StringVar(value='MPC2000XL-inspired')
        self.start = tk.StringVar(value='0')
        self.duration = tk.StringVar(value='8')
        self.pitch = tk.DoubleVar(value=0)
        self.status = tk.StringVar(value='Load a 16-bit WAV to begin. No audio will be uploaded.')

        header = ttk.Frame(self.root, padding=14)
        header.pack(fill='x')
        ttk.Label(header, text='VINTAGE SAMPLER LAB', font=('Helvetica',22,'bold')).pack(anchor='w')
        ttk.Label(header, text='Five sampler-inspired voices · 16 playable pads · export a kit for Ableton / FL Studio').pack(anchor='w')

        controls = ttk.Frame(self.root, padding=14)
        controls.pack(fill='x')
        ttk.Button(controls, text='Load 16-bit WAV', command=self.choose_sample).grid(row=0,column=0,padx=4,pady=4)
        ttk.Combobox(controls, textvariable=self.profile, values=list(PROFILES), state='readonly', width=25).grid(row=0,column=1,columnspan=3,padx=4,pady=4)
        ttk.Label(controls, text='Start (s)').grid(row=1,column=0)
        ttk.Entry(controls, textvariable=self.start, width=10).grid(row=1,column=1)
        ttk.Label(controls, text='Length (s)').grid(row=1,column=2)
        ttk.Entry(controls, textvariable=self.duration, width=10).grid(row=1,column=3)
        ttk.Label(controls, text='Varispeed pitch (semitones)').grid(row=2,column=0,columnspan=2,sticky='w')
        ttk.Scale(controls, from_=-12, to=12, variable=self.pitch, orient='horizontal', length=210).grid(row=2,column=2,columnspan=2)

        pads = ttk.Frame(self.root, padding=14)
        pads.pack(fill='both',expand=True)
        for n in range(16):
            b = ttk.Button(pads,text=f'PAD {n+1:02d}',command=lambda n=n:self.play_pad(n))
            b.grid(row=n//4,column=n%4,padx=5,pady=5,sticky='nsew')
        for n in range(4):
            pads.columnconfigure(n,weight=1)
            pads.rowconfigure(n,weight=1)

        lower = ttk.Frame(self.root,padding=14)
        lower.pack(fill='x')
        ttk.Button(lower,text='Export all 16 WAV pads',command=self.export).pack(side='left')
        ttk.Button(lower,text='Stop preview',command=self.stop_preview).pack(side='left',padx=8)
        ttk.Label(lower,textvariable=self.status,wraplength=470).pack(side='left',padx=10)
        self.root.protocol('WM_DELETE_WINDOW',self.close)

    def choose_sample(self):
        name = filedialog.askopenfilename(title='Choose a WAV',filetypes=[('Uncompressed WAV','*.wav'),('All files','*')])
        if not name:
            return
        try:
            info=probe(name)
        except Exception as exc:
            messagebox.showerror('Unsupported WAV',str(exc));return
        self.sample=Path(name)
        self.status.set(f'{self.sample.name} · {info["duration"]}s · {info["rate"]} Hz')

    def generate(self,folder):
        if self.sample is None:
            raise ValueError('Load a sample first.')
        return export_kit(self.sample,folder,self.profile.get(),int(round(self.pitch.get())),
                          float(self.start.get()),float(self.duration.get()),16)

    def play_pad(self,index):
        try:
            self.generate(self.preview_dir.name)
            self.stop_preview()
            file=Path(self.preview_dir.name)/f'pad_{index+1:02d}.wav'
            self.preview_process=subprocess.Popen(['/usr/bin/afplay',str(file)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.status.set(f'Playing pad {index+1} from {self.profile.get()}')
        except Exception as exc:
            messagebox.showerror('Preview unavailable',str(exc))

    def export(self):
        folder=filedialog.askdirectory(title='Choose an empty output folder')
        if not folder:
            return
        try:
            result=self.generate(folder)
            self.status.set(f'Exported {result["padCount"]} pads and kit.json to {folder}')
        except Exception as exc:
            messagebox.showerror('Export failed',str(exc))

    def stop_preview(self):
        if self.preview_process is not None and self.preview_process.poll() is None:
            self.preview_process.terminate()
        self.preview_process=None

    def close(self):
        self.stop_preview()
        self.preview_dir.cleanup()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__=='__main__':
    SamplerApp().run()
