#!/usr/bin/env python3
"""Install the built bridge for this user; retain the system HAL and service args."""
import argparse,datetime,hashlib,os,plistlib,shutil,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true');p.add_argument('--defer-restart',action='store_true');a=p.parse_args()
    if not a.run:print('Dry run: --run required; no service/files changed');return
    root=Path(__file__).resolve().parents[1];source=root/'build/kore1-final-bridge' if (root/'build/kore1-final-bridge').is_file() else root/'kore1-final-bridge'
    agent=Path.home()/'Library/LaunchAgents/org.nikore.bridge.plist'
    old=agent.read_bytes();config=plistlib.loads(old)
    assert config['Label']=='org.nikore.bridge'
    assert config['ProgramArguments'][1:]==['--run','--coreaudio-pcm','--continuous','--mixxx-feedback','--main-gain','0.2','--headphone-gain','1'], 'Unexpected service arguments; leave unchanged'
    assert source.is_file() and os.access(source,os.X_OK)
    destination=Path.home()/'Library/Application Support/KORE1/kore1-final-bridge'
    destination.parent.mkdir(parents=True,exist_ok=True)
    backups=destination.parent/'Backups';backups.mkdir(exist_ok=True)
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    (backups/('agent-'+stamp+'.plist')).write_bytes(old)
    previous=backups/('bridge-'+stamp)
    if destination.exists():shutil.copy2(destination,previous)
    temporary=destination.with_suffix('.new');shutil.copy2(source,temporary);temporary.chmod(0o755)
    service=f'gui/{os.getuid()}/org.nikore.bridge';domain=f'gui/{os.getuid()}'
    if a.defer_restart:
        if config['ProgramArguments'][0]!=str(destination):
            temporary.unlink();raise RuntimeError('Deferred replacement requires an already configured user-local bridge')
        os.replace(temporary,destination)
        print('User bridge updated atomically; running process unchanged until next service restart:',destination)
        print('SHA256:',hashlib.sha256(destination.read_bytes()).hexdigest());return
    stopped=False;replaced=False
    try:
        subprocess.run(['launchctl','bootout',service],check=True);stopped=True
        for _ in range(60):
            if subprocess.run(['launchctl','print',service],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:break
            time.sleep(.1)
        else:raise RuntimeError('Service did not stop')
        os.replace(temporary,destination);replaced=True
        config['ProgramArguments'][0]=str(destination)
        agent.write_bytes(plistlib.dumps(config))
        subprocess.run(['launchctl','bootstrap',domain,str(agent)],check=True);stopped=False
        print('User bridge:',destination)
        print('SHA256:',hashlib.sha256(destination.read_bytes()).hexdigest())
        print('System HAL unchanged; service arguments preserved. Agent backup:',backups/('agent-'+stamp+'.plist'))
    except BaseException:
        agent.write_bytes(old)
        if replaced and previous.exists():shutil.copy2(previous,destination)
        if stopped:subprocess.run(['launchctl','bootstrap',domain,str(agent)],check=True)
        raise
    finally:
        if temporary.exists():temporary.unlink()
if __name__=='__main__':main()
