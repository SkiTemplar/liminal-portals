# Resume Saved/Thermal (gpu.csv de nvidia-smi, counters.csv y phases.csv de run_bench.ps1) por fase y por minuto.
import csv, datetime as dt, statistics as st
ph=[r for r in csv.DictReader(open('Saved/Thermal/phases.csv',encoding='utf-8-sig'))]
P=[(r['phase'],dt.datetime.fromisoformat(r['start'][:26]).replace(tzinfo=None),dt.datetime.fromisoformat(r['end'][:26]).replace(tzinfo=None)) for r in ph]
rows=list(csv.reader(open('Saved/Thermal/gpu.csv')))[1:]
def num(s): return float(s.strip().split()[0])
data=[]
for r in rows:
    t=dt.datetime.strptime(r[0].strip(),'%Y/%m/%d %H:%M:%S.%f')
    data.append((t,num(r[1]),num(r[2]),num(r[4]),num(r[5]),r[7].strip(),r[8].strip(),r[9].strip(),r[10].strip()))
print('fase      n  T_med T_max T_p95  MHz_med MHz_min  W_med  util  hwThermal swThermal powerCap')
for name,a,b in P:
    d=[x for x in data if a<=x[0]<b]
    if not d: continue
    T=[x[1] for x in d]; C=[x[2] for x in d]; W=[x[3] for x in d]; U=[x[4] for x in d]
    q=sorted(T)[int(0.95*(len(T)-1))]
    act=lambda i: sum(1 for x in d if x[i]=='Active')
    print(f'{name:7} {len(d):4} {st.mean(T):6.1f} {max(T):5.0f} {q:5.0f} {st.median(C):8.0f} {min(C):7.0f} {st.mean(W):6.1f} {st.mean(U):5.0f} {act(6):9} {act(7):9} {act(8):8}')
print()
rows=list(csv.reader(open('Saved/Thermal/counters.csv',encoding='utf-8-sig')))[1:]
cd=[]
for r in rows:
    if len(r)<5 or r[1].startswith('ERR'): continue
    t=dt.datetime.fromisoformat(r[0][:26]); cd.append((t,float(r[1])/10-273.15,float(r[2]),float(r[3]),float(r[4])))
print('fase      n  TZ_med TZ_max  perf_med perf_min  util  limit_min')
for name,a,b in P:
    d=[x for x in cd if a<=x[0]<b]
    if not d: continue
    print(f'{name:7} {len(d):4} {st.mean([x[1] for x in d]):6.1f} {max(x[1] for x in d):6.1f} {st.median([x[2] for x in d]):8.0f} {min(x[2] for x in d):8.0f} {st.mean([x[3] for x in d]):5.0f} {min(x[4] for x in d):8.0f}')
print()
for name in ('cpu','combo'):
    a,b=[(x[1],x[2]) for x in P if x[0]==name][0]
    print(name,'por minuto: TZ_max | GPU_T_max | GPU_MHz_med | perf_med')
    m=0
    while a+dt.timedelta(minutes=m)<b:
        s,e=a+dt.timedelta(minutes=m),a+dt.timedelta(minutes=m+1)
        c=[x for x in cd if s<=x[0]<e]; g=[x for x in data if s<=x[0]<e]
        if c and g: print(f'  {m:2}: {max(x[1] for x in c):5.1f} | {max(x[1] for x in g):3.0f} | {st.median([x[2] for x in g]):5.0f} | {st.median([x[2] for x in c]):4.0f}')
        m+=1
