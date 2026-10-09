"""Incremental Java/DEX build; cache receipts verify every input and output byte."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def hashes(paths):
    return {str(path.resolve()):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(paths)}

class BuildCache:
    def __init__(self,path): self.path=Path(path)
    def valid(self,inputs,outputs,options):
        if not outputs or not all(p.is_file() for p in inputs+outputs): return False
        try: record=json.loads(self.path.read_text(encoding='utf-8'))
        except (OSError,ValueError): return False
        return record=={'inputs':hashes(inputs),'outputs':hashes(outputs),'options':options}
    def save(self,inputs,outputs,options):
        self.path.parent.mkdir(parents=True,exist_ok=True)
        temporary=self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps({'inputs':hashes(inputs),'outputs':hashes(outputs),'options':options},sort_keys=True),encoding='utf-8')
        temporary.replace(self.path)

def reset_directory(path):
    managed=(ROOT/'build/java').resolve();target=path.resolve()
    if target==managed or not target.is_relative_to(managed): raise ValueError('cleanup outside managed Java output')
    if target.exists(): shutil.rmtree(target)
    target.mkdir(parents=True)

def run(args):
    subprocess.run([str(x) for x in args],check=True)

def build(jdk,sdk):
    java=jdk/'bin/java.exe';javac=jdk/'bin/javac.exe'
    android=sdk/'platforms/android-34/android.jar';d8=sdk/'build-tools/34.0.0/lib/d8.jar'
    sources=sorted((ROOT/'menu').glob('*.java'))
    inputs=sources+[android,d8,java,javac,jdk/'release',Path(__file__)]
    classes=ROOT/'build/java/menu-classes';dex=ROOT/'build/java/dex';tests=ROOT/'build/java/test-classes'
    options={'source':8,'target':8,'minApi':26,'mode':'release'}
    cache=BuildCache(ROOT/'build/java/menu-cache.json')
    outputs=sorted(classes.rglob('*.class'))+[dex/'classes.dex']
    if cache.valid(inputs,outputs,options): print('Menu compile cache: HIT',flush=True)
    else:
        print('Menu compile cache: MISS',flush=True)
        reset_directory(classes);reset_directory(dex)
        run([javac,'-source','8','-target','8','-encoding','UTF-8','-classpath',android,'-d',classes]+sources)
        classfiles=sorted(classes.rglob('*.class'))
        run([java,'-cp',d8,'com.android.tools.r8.D8','--release','--min-api','26','--lib',android,'--output',dex]+classfiles)
        cache.save(inputs,classfiles+[dex/'classes.dex'],options)
    shutil.copyfile(dex/'classes.dex',ROOT/'menu/wsm_menu.dex')
    test_sources=sorted((ROOT/'tests').glob('*Test.java'))
    if not test_sources: raise ValueError('No Java regression suites discovered')
    test_inputs=inputs+test_sources+sorted(classes.rglob('*.class'))
    test_cache=BuildCache(ROOT/'build/java/test-cache.json')
    test_outputs=sorted(tests.rglob('*.class'))
    if not test_cache.valid(test_inputs,test_outputs,options):
        reset_directory(tests)
        run([javac,'-source','8','-target','8','-encoding','UTF-8','-classpath',classes,'-d',tests]+test_sources)
        test_cache.save(test_inputs,sorted(tests.rglob('*.class')),options)
    for source in test_sources:
        match=re.search(r'^\s*package\s+([\w.]+)\s*;',source.read_text(encoding='utf-8'),re.M)
        name=(match.group(1)+'.' if match else '')+source.stem
        run([java,'-ea','-cp',str(tests)+';'+str(classes),name])
    print('Java regression suites executed: '+str(len(test_sources)),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--jdk',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    a=p.parse_args();build(a.jdk,a.sdk)
