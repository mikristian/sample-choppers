"""Dependency-free four-by-four-pad MIDI sequencer and portable pattern format.

This recreates a *workflow*, not proprietary Maschine, Akai or Ensoniq code.
MIDI notes 36–51 correspond to the kit's 16 exported WAV pads.
"""

from dataclasses import dataclass, field
import json
from pathlib import Path
import struct

PAD_KEYS = ('1','2','3','4','q','w','e','r','a','s','d','f','z','x','c','v')
DEFAULT_NOTE = 36


def vlq(value):
    """Encode nonnegative SMF variable length quantity."""
    if value < 0 or value > 0x0fffffff:
        raise ValueError('MIDI VLQ value outside defined bounds')
    buf = [value & 0x7f]
    value >>= 7
    while value:
        buf.insert(0, 0x80 | (value & 0x7f))
        value >>= 7
    return bytes(buf)


@dataclass
class PadPattern:
    bpm: float = 128.0
    bars: int = 2
    hits: set = field(default_factory=set)
    def __post_init__(self):
        self.bpm = float(self.bpm)
        self.bars = int(self.bars)
        if not 40 <= self.bpm <= 240 or self.bars not in (1,2,4,8):
            raise ValueError('BPM must be 40–240 and bars 1, 2, 4 or 8')
        existing=list(self.hits)
        self.hits=set()
        for pad,step in existing:
            self.set_hit(pad,step)
    @property
    def steps(self):
        return self.bars * 16
    @property
    def step_seconds(self):
        return 60.0 / self.bpm / 4.0
    @property
    def duration_seconds(self):
        return self.step_seconds * self.steps
    def set_hit(self,pad,step,on=True):
        pad=int(pad); step=int(step)
        if not 0 <= pad < 16 or not 0 <= step < self.steps:
            raise ValueError('Pad or sequencer step outside permitted range')
        if on: self.hits.add((pad,step))
        else: self.hits.discard((pad,step))
    def toggle_hit(self,pad,step):
        self.set_hit(pad,step,(pad,step) not in self.hits)
        return (pad,step) in self.hits
    def record_at(self,pad,elapsed,quantize=True):
        """Place a triggered pad into the loop, quantized to 16th-note steps."""
        if elapsed < 0: elapsed=0.0
        index=elapsed/self.step_seconds
        step=(int(round(index)) if quantize else int(index)) % self.steps
        self.set_hit(pad,step)
        return step
    def data(self):
        return {'format':'VintageSamplerLab-Pattern-1','bpm':self.bpm,'bars':self.bars,
                'pads':{'first_midi_note':DEFAULT_NOTE,'count':16,'keys':list(PAD_KEYS)},
                'hits':[{'pad':pad+1,'step':step+1,'midi_note':DEFAULT_NOTE+pad}
                        for pad,step in sorted(self.hits,key=lambda x:(x[1],x[0]))]}
    def write_json(self,path):
        Path(path).write_text(json.dumps(self.data(),indent=2)+'\n')
        return Path(path)
    @classmethod
    def from_json(cls,path):
        data=json.loads(Path(path).read_text())
        if data.get('format')!='VintageSamplerLab-Pattern-1':
            raise ValueError('Unrecognized pad pattern file')
        return cls(data['bpm'],data['bars'],{(h['pad']-1,h['step']-1) for h in data['hits']})

    def write_midi(self,path,velocity=105,gate_steps=1,repeat=1):
        """Write format-0 Standard MIDI File usable by Ableton and FL Studio.

        480 ticks per quarter, 120 ticks per 16th. Multiple pads can fire on the
        same step. On/off sorting never leaves hanging notes on the last step.
        """
        if not 1 <= int(repeat) <= 64 or not 1 <= int(gate_steps) <= 8:
            raise ValueError('Repeat/gate out of range')
        if not 1 <= int(velocity) <= 127:
            raise ValueError('Velocity out of MIDI range')
        def event(absolute,priority,message):
            return (int(absolute),priority,bytes(message))
        tempo=int(round(60000000/self.bpm))
        track=[event(0,-3,b'\xff\x51\x03'+tempo.to_bytes(3,'big')),
               event(0,-2,b'\xff\x58\x04\x04\x02\x18\x08')]
        for iteration in range(int(repeat)):
            offset=iteration*self.steps
            for pad,step in self.hits:
                on=(offset+step)*120
                note=DEFAULT_NOTE+pad
                track.append(event(on,1,(0x99,note,int(velocity)))) # MIDI channel 10 for drum kit
                track.append(event(on+int(gate_steps)*120,0,(0x89,note,0)))
        end=self.steps*int(repeat)*120+int(gate_steps)*120
        track.append(event(end,2,b'\xff\x2f\x00'))
        output=bytearray(); last=0
        for tick,priority,msg in sorted(track):
            output.extend(vlq(tick-last));output.extend(msg);last=tick
        path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(b'MThd'+struct.pack('>IHHH',6,0,1,480)+b'MTrk'+struct.pack('>I',len(output))+output)
        return path
