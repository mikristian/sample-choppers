"""Original ASR-10-*inspired* keyzone sampler with WAV export.

This is a modern implementation of generic multisample keyboard concepts. It
does NOT emulate or contain Ensoniq firmware, OS, ESP DSP or proprietary ROMs.
"""

from dataclasses import dataclass,asdict,field
import json
import math
from pathlib import Path

from sampler_engine import read_excerpt,process,save_wav


@dataclass(frozen=True)
class KeyZone:
    source: str
    root_note: int=60
    low_note: int=36
    high_note: int=96
    min_velocity: int=1
    max_velocity: int=127
    gain: float=1.0
    start_seconds: float=0.0
    duration_seconds: float=3.0

    def __post_init__(self):
        if not 0<=self.low_note<=self.root_note<=self.high_note<=127:
            raise ValueError('Root pitch must be inside a valid MIDI note zone')
        if not 1<=self.min_velocity<=self.max_velocity<=127:
            raise ValueError('Invalid velocity layer range')
        if not 0<=self.gain<=2:
            raise ValueError('Gain must be between 0 and 2')
        if self.start_seconds<0 or not 0<self.duration_seconds<=12:
            raise ValueError('Start must be >=0 and excerpt length 0–12 seconds')

    def matches(self,note,velocity):
        return self.low_note <= note <= self.high_note and self.min_velocity <= velocity <= self.max_velocity


@dataclass
class KeyboardProgram:
    name: str='ASR-style sample keyboard'
    zones: list=field(default_factory=list)
    attack_seconds: float=0.007
    release_seconds: float=0.12

    def __post_init__(self):
        if not 0 <= self.attack_seconds <= 2 or not 0 <= self.release_seconds <= 3:
            raise ValueError('Attack/release outside supported limits')

    def render(self,note,velocity=100,rate=44100):
        if not 0<=int(note)<=127 or not 0<=int(velocity)<=127:
            raise ValueError('MIDI note/velocity outside range')
        if velocity==0:
            return [0.0]
        layers=[]
        for zone in self.zones:
            if not zone.matches(note,velocity):continue
            samples,source_rate=read_excerpt(zone.source,zone.start_seconds,zone.duration_seconds)
            offset=int(note)-int(zone.root_note)
            # Pure varispeed, resampling and soft filtering, not the Ensoniq DAC.
            layer=process(samples,source_rate,'ASR10-inspired',offset,rate)
            gain=zone.gain*math.sqrt(velocity/127.0)
            layers.append([x*gain for x in layer])
        if not layers:
            raise ValueError('No sample zone covers this key/velocity')
        n=max(map(len,layers))
        result=[0.0]*n
        for layer in layers:
            for i,x in enumerate(layer):result[i]+=x
        a=min(n//2,round(rate*self.attack_seconds))
        r=min(n//2,round(rate*self.release_seconds))
        for i in range(a): result[i]*=i/max(1,a)
        for i in range(r): result[-i-1]*=i/max(1,r)
        return [max(-1.0,min(0.999969,x)) for x in result]

    def export_note(self,destination,note,velocity=100,rate=44100):
        return save_wav(destination,self.render(note,velocity,rate),rate)

    def save_program(self,path):
        target=Path(path)
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps({'format':'VintageSamplerLab-ASR-keyzone-program-1',
          'name':self.name,'attack_seconds':self.attack_seconds,
          'release_seconds':self.release_seconds,
          'zones':[asdict(zone) for zone in self.zones]},indent=2)+'\n')
        return target

    @classmethod
    def load_program(cls,path):
        data=json.loads(Path(path).read_text())
        if data.get('format')!='VintageSamplerLab-ASR-keyzone-program-1':
            raise ValueError('Unknown keyzone program')
        return cls(data['name'],[KeyZone(**x) for x in data['zones']],
                   data['attack_seconds'],data['release_seconds'])
