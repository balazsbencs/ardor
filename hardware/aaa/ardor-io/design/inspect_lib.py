import sexpdata as s
from pathlib import Path
root=Path('/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols')
def key(x): return str(x[0]) if isinstance(x,list) and x else ''
def get(lib,name):
 d=s.loads((root/(lib+'.kicad_sym')).read_text()); n=next(x for x in d if key(x)=='symbol' and x[1]==name)
 ext=next((x[1] for x in n if key(x)=='extends'),None)
 if ext:
  b=get(lib,ext); n=[x for x in n if key(x)!='extends']+[[x[0],x[1].replace(ext,name),*x[2:]] for x in b if key(x)=='symbol']
 return n
if __name__=='__main__':
 for lib,name in [('Amplifier_Operational','OPA2320'),('Amplifier_Audio','TPA6132A2RTE'),('Analog_ADC','ADS1115IDGS'),('Isolator','H11L1'),('Device','R'),('Device','C'),('Device','D_Schottky'),('Connector','Conn_01x03_Pin'),('Connector','DIN-5_180degree'),('Connector_Audio','AudioJack3'),('Switch','SW_DPDT_x2')]:
  try:
   n=get(lib,name); print(lib,name)
   for u in n:
    if key(u)=='symbol':
     print(u[1], [(next(v[1] for v in p if key(v)=='number'),next(v[1] for v in p if key(v)=='name'),next(v[1:] for v in p if key(v)=='at'), str(p[1])) for p in u if key(p)=='pin'])
  except Exception as e: print(e)
