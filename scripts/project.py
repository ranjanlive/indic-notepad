"""Portable IndicStudio project commands. Local only; never pushes, publishes or uploads."""
from __future__ import annotations
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,os,re,shutil,subprocess,sys,threading,time,zipfile
from contextlib import contextmanager
from jsonschema import Draft202012Validator,FormatChecker

DEFAULT=Path(__file__).resolve().parent.parent
SKIP_DIRS={'.git','.gradle','.kotlin','.idea','build','.cxx','.externalNativeBuild','__pycache__','node_modules','target','dist'}
ROOT_FILES={'project.json','README.md','AGENTS.md','CHANGELOG.md','SECURITY.md','NOTICE.md','LICENSE','CONTRIBUTING.md','THIRD_PARTY_NOTICES.md','.gitignore','.gitattributes'}
PUBLIC_DIRS={'source_code','assets','docs','scripts','.github','release'}
PRIVATE_SUFFIX={'.jks','.keystore','.p12','.pfx','.pem','.key','.p8'}
TOKEN_PATTERNS=[('private-key',re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----')),('github-token',re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{35,})\b')),('api-token',re.compile(r'\b(?:sk-[A-Za-z0-9_-]{25,}|AIza[A-Za-z0-9_-]{30,})\b'))]

def now():return datetime.now(timezone.utc).isoformat()
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def dump(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def inside(root,relative):
    p=(root/relative).resolve()
    if not p.is_relative_to(root.resolve()) or p==root.resolve():raise ValueError('Path escapes project')
    return p
def schema(root,data,name):
    validator=Draft202012Validator(read(root/'docs/schemas'/name),format_checker=FormatChecker())
    errors=list(validator.iter_errors(data))
    if errors:raise ValueError('Schema failure: '+name+'; fields: '+', '.join('.'.join(map(str,e.path)) for e in errors))
def private_reason(relative):
    parts=relative.parts;name=relative.name.lower();suffix=relative.suffix.lower()
    if any(p in SKIP_DIRS for p in parts):return 'generated state or Git internals'
    if parts[:3]==('source_code','src-tauri','gen'):return 'generated Tauri schemas'
    if len(parts)>1 and parts[:2]==('release','private'):return 'private release material'
    if len(parts)>1 and parts[:2]==('docs','archive'):return 'local historical documents'
    if parts and parts[0]=='release' and 'builds' in parts:return 'binary release material'
    if suffix in PRIVATE_SUFFIX:return 'private key or keystore'
    if suffix in {'.apk','.aab','.exe','.msi','.dmg','.pkg','.deb','.rpm','.appimage'}:return 'binary build'
    if name in {'local.properties','signing.properties','keystore.properties','secrets.properties','credentials.json','.scaffold.json'}:return 'machine or secret configuration'
    if name.startswith(('.env','service-account','service_account','client_secret')) and not name.endswith('.example'):return 'secret configuration'
    if suffix in {'.log','.pyc','.iml'} or ('docs','verification')==parts[:2] and name.startswith('local-'):return 'local verification output'
    return None
def public_files(root):
    result=[]
    for directory,dirs,names in os.walk(root):
        base=Path(directory)
        dirs[:]=[d for d in dirs if not private_reason((base/d).relative_to(root)) and ((base/d).relative_to(root).parts[0] in PUBLIC_DIRS)]
        for name in names:
            p=base/name;rel=p.relative_to(root)
            if private_reason(rel):continue
            if rel.parts[0] not in PUBLIC_DIRS and rel.as_posix() not in ROOT_FILES:continue
            if p.is_symlink() or not p.resolve().is_relative_to(root.resolve()):raise ValueError('Symlink or escaped public file')
            result.append(p)
    return sorted(result)
def scan(root,files):
    findings=[]
    text_suffix={'.kt','.kts','.java','.xml','.json','.properties','.toml','.md','.txt','.yml','.yaml','.py','.ps1','.js','.mjs','.cjs','.ts','.rs','.cpp','.h','.c','.rc','.nsi','.cmake','.html','.css','.svg','.sh','.bat'}
    for p in files:
        if p.suffix.lower() not in text_suffix or p.stat().st_size>2*1024*1024:continue
        try:text=p.read_text(encoding='utf-8-sig')
        except UnicodeError:continue
        for kind,pattern in TOKEN_PATTERNS:
            if pattern.search(text):findings.append({'file':p.relative_to(root).as_posix(),'reason':kind})
    return findings
def source_manifest(root):
    source=inside(root,'source_code')
    entries=[{'file':p.relative_to(source).as_posix(),'sha256':digest(p),'bytes':p.stat().st_size} for p in public_files(root) if p.is_relative_to(source)]
    combined=hashlib.sha256(''.join(f'{x["sha256"]}  {x["file"]}\n' for x in entries).encode()).hexdigest()
    return {'schema_version':1,'tree_sha256':combined,'files':entries}
def candidate_git(root):
    if not (root/'.git').is_dir():return []
    result=subprocess.run(['git','-C',str(root),'ls-files','--cached','-z'],capture_output=True,check=True)
    return [Path(p) for p in result.stdout.decode('utf-8').split('\0') if p]

def validate(root,quiet=False,require_builds=False):
    root=root.resolve();project=read(root/'project.json');schema(root,project,'project.schema.json')
    if root.name!=project['slug']:raise ValueError('Project folder and slug differ')
    for name in ['source_code','assets','docs','release']:
        if not inside(root,name).is_dir():raise ValueError('Missing required directory: '+name)
    tracked_bad=[{'file':p.as_posix(),'reason':private_reason(p)} for p in candidate_git(root) if private_reason(p)]
    if tracked_bad:raise ValueError('Private/generated files are already tracked; refusing packaging. Paths: '+', '.join(p['file'] for p in tracked_bad))
    files=public_files(root);findings=scan(root,files)
    if findings:raise ValueError('Credential-pattern findings (values withheld): '+json.dumps(findings))
    metadata=read(inside(root,project['paths']['website_metadata']));privacy=read(inside(root,project['paths']['privacy_content']))
    schema(root,metadata,'product.schema.json');schema(root,privacy,'privacy.schema.json')
    if metadata['slug']!=project['slug'] or metadata['type']!=project['type']:raise ValueError('Website identity differs from project')
    release_count=0;artifact_count=0;missing_builds=0
    for p in sorted((root/'release').glob('*/release.json')):
        release=read(p);schema(root,release,'release.schema.json')
        if release['release_id']!=p.parent.name or release['product']!=project['slug']:raise ValueError('Release identity mismatch')
        for artifact in release['artifacts']:
            file=inside(p.parent,artifact['file'])
            if not file.is_file() and not require_builds:missing_builds+=1;continue
            if not file.is_file() or file.stat().st_size!=artifact['bytes'] or digest(file)!=artifact['sha256']:raise ValueError('Release artifact changed: '+artifact['file'])
            artifact_count+=1
        for record in (p.parent/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
            expected,relative=record.split('  ',1)
            if not inside(p.parent,relative).is_file() and not require_builds and relative in {a['file'] for a in release['artifacts']}:continue
            if digest(inside(p.parent,relative))!=expected:raise ValueError('Release snapshot changed: '+relative)
        release_count+=1
    if 'android' in project['platforms'] and (root/'source_code/gradlew.bat').exists():
        props=(root/'source_code/gradle.properties').read_text(encoding='utf-8-sig')
        for setting in ['org.gradle.workers.max=2','org.gradle.parallel=false','org.gradle.daemon=false','kotlin.compiler.execution.strategy=in-process']:
            if setting not in props:raise ValueError('Resource setting missing: '+setting)
        if '-Xmx1536m' not in props and '-Xmx2048m' not in props:raise ValueError('Expected recorded Java heap limit')
    report={'passed':True,'product':project['slug'],'public_file_count':len(files),'release_count':release_count,'artifacts_verified':artifact_count,'binary_files_not_present':missing_builds,'credential_pattern_findings':len(findings),'scan_limit':'Filename guards and selected high-confidence text patterns; not a comprehensive secret-scanning certification.'}
    if not quiet:print(json.dumps(report))
    return report

def package(root,destination):
    validate(root,quiet=True)
    project=read(root/'project.json');destination=destination.resolve();destination.mkdir(parents=True,exist_ok=True)
    target=destination/(project['slug']+'-github-source.zip')
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=7) as z:
        for p in public_files(root):
            info=zipfile.ZipInfo(p.relative_to(root).as_posix());info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=(0o100755 if p.name=='gradlew' or p.suffix=='.sh' else 0o100644)<<16
            z.writestr(info,p.read_bytes())
    with zipfile.ZipFile(target) as z:
        if z.testzip():raise ValueError('Archive CRC failure')
        for name in z.namelist():
            if private_reason(Path(name)):raise ValueError('Forbidden file in source archive')
    result={'product':project['slug'],'file':str(target),'bytes':target.stat().st_size,'sha256':digest(target),'secrets_included':False,'binaries_included':False}
    print(json.dumps(result));return result

def stage_release(root,release_id,apk=None,aab=None,channel='preview',origin_note=None,extra_artifacts=None):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-z0-9.]+)?',release_id):raise ValueError('Use a semantic release identifier')
    if channel not in ('preview','beta'):raise ValueError('This local staging command does not certify or publish production releases')
    project=read(root/'project.json');target=inside(root,'release/'+release_id)
    if target.exists():raise ValueError('Release already exists; never overwrite a versioned snapshot')
    validate(root,quiet=True)
    inputs=[(apk,'builds/android/app-debug.apk','debug-apk'),(aab,'builds/android/app-release-unsigned.aab','release-aab')]
    for os_name,path in extra_artifacts or []:
        if os_name not in project['platforms']:raise ValueError('Artifact platform is not declared by project.json')
        path=Path(path)
        if not re.search(r'\.(apk|aab|exe|msi|dmg|pkg|zip|deb|rpm|appimage|tar\.gz|tar\.xz)$',path.name,re.I):raise ValueError('Unsupported installer/archive format')
        safe_name=re.sub(r'[^A-Za-z0-9._+-]','-',path.name)
        inputs.append((path,'builds/'+os_name+'/'+safe_name,'archive' if re.search(r'\.(zip|tar\.gz|tar\.xz)$',path.name,re.I) else 'installer'))
    if not any(x[0] for x in inputs):raise ValueError('Provide at least one real build artifact')
    for source,_,kind in inputs:
        if not source:continue
        source=Path(source)
        if not source.is_file():raise ValueError('Missing input artifact; no snapshot created')
        if kind in ('debug-apk','release-aab') and not zipfile.is_zipfile(source):raise ValueError('Invalid Android archive; no snapshot created')
    filenames=[x[1] for x in inputs if x[0]]
    if len(set(filenames))!=len(filenames):raise ValueError('Duplicate release artifact filename')
    gradle=root/'source_code/app/build.gradle.kts';version_name=release_id.split('-')[0];version_code=None
    if gradle.exists():
        text=gradle.read_text(encoding='utf-8-sig');match=re.search(r'versionName\s*=\s*"([^"]+)"',text);code=re.search(r'versionCode\s*=\s*(\d+)',text)
        if match:version_name=match[1]
        if code:version_code=int(code[1])
    target.mkdir(parents=True);artifacts=[]
    for source,filename,kind in inputs:
        if not source:continue
        source=Path(source).resolve()
        dest=inside(target,filename);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
        signing='debug-build-not-verified' if kind=='debug-apk' else 'not-verified'
        if kind=='release-aab':
            with zipfile.ZipFile(dest) as z:
                if not any(re.match(r'META-INF/[^/]+\.(RSA|DSA|EC|SF)$',n,re.I) for n in z.namelist()):signing='unsigned'
        # A signed bundle must not be relabeled unsigned.
        if kind=='release-aab' and signing!='unsigned':
            renamed=dest.with_name('app-release-signature-not-verified.aab');dest.rename(renamed);dest=renamed;filename=dest.relative_to(target).as_posix()
        artifacts.append({'file':filename,'kind':kind,'bytes':dest.stat().st_size,'sha256':digest(dest),'signing_status':signing})
    for filename,key in [('metadata.json','website_metadata'),('privacy_content.json','privacy_content')]:
        data=read(inside(root,project['paths'][key]));data['$schema']='../../docs/schemas/'+('product.schema.json' if key=='website_metadata' else 'privacy.schema.json');dump(target/filename,data)
    listing=root/'docs/store/store-listing.json'
    if listing.exists():shutil.copy2(listing,target/'store-listing.json')
    manifest=source_manifest(root);dump(target/'source-manifest.json',manifest)
    commit=None
    if (root/'.git').exists():
        r=subprocess.run(['git','-C',str(root),'rev-parse','--verify','HEAD'],capture_output=True,text=True)
        if r.returncode==0:
            status=subprocess.run(['git','-C',str(root),'status','--porcelain','--','source_code'],capture_output=True,text=True,check=True)
            if not status.stdout.strip():commit=r.stdout.strip()
    release={'$schema':'../../docs/schemas/release.schema.json','schema_version':1,'release_id':release_id,'product':project['slug'],'channel':channel,'version_name':version_name,'version_code':version_code,'package_name':project.get('package_name'),'created':now(),'source':{'tree_sha256':manifest['tree_sha256'],'git_commit':commit,'manifest':'source-manifest.json'},'artifacts':artifacts,'metadata':'metadata.json','privacy_content':'privacy_content.json','published':False,'notes':['Local staged build; not a public store release. Debug APK certificates have not been verified by this staging command.']+([origin_note] if origin_note else [])}
    dump(target/'release.json',release)
    hashed=sorted(p for p in target.rglob('*') if p.is_file())
    (target/'SHA256SUMS.txt').write_text(''.join(f'{digest(p)}  {p.relative_to(target).as_posix()}\n' for p in hashed),encoding='utf-8')
    project['current_release']=release_id;dump(root/'project.json',project)
    dump(root/'release/latest.json',{'schema_version':1,'release_id':release_id,'manifest':release_id+'/release.json','channel':channel,'published':False})
    return release

def sync_website(root,website,apply=False):
    validate(root,quiet=True);project=read(root/'project.json');website=website.resolve()
    if not (website/'site.config.json').is_file() or not (website/'scripts/build.py').is_file():raise ValueError('Not an IndicStudio website source directory')
    target=inside(website,project['type']+'/'+project['slug'])
    metadata=read(root/'docs/website/metadata.json');references=[metadata['icon_url']]
    if metadata.get('social_image_url'):references.append(metadata['social_image_url'])
    for shots in metadata['screenshots_url'].values():references.extend(s['url'] for s in shots)
    if metadata.get('apk_file_url'):references.append(metadata['apk_file_url'])
    for platform in metadata['platforms']:references.extend(dl['url'] for dl in platform.get('downloads',[]))
    local=[]
    for value in references:
        if value.startswith(('https://','/')):continue
        if not value.startswith('assets/'):raise ValueError('Sync supports local assets/ references; use HTTPS release URLs for installers')
        source=inside(root,value)
        if not source.is_file():raise ValueError('Missing referenced project asset: '+value)
        if private_reason(Path(value)):raise ValueError('Private or binary material cannot be synced as a website image')
        local.append((source,value))
    plan={'product':project['slug'],'target':str(target),'action':'apply' if apply else 'preview','asset_count':len(local),'assets':'Only referenced project assets -> website product assets/','metadata':'docs/website/{metadata.json,privacy_content.json} -> website product JSON'}
    if apply:
        target.mkdir(exist_ok=True,parents=True)
        for source,relative in local:
            dest=inside(target,relative);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
        for name,schema_name in [('metadata.json','product.schema.json'),('privacy_content.json','privacy.schema.json')]:
            data=read(root/'docs/website'/name);data['$schema']='../../schemas/'+schema_name;dump(target/name,data)
    print(json.dumps(plan));return plan

@contextmanager
def build_slot(root):
    import psutil
    lock=root/'.tools-lock'
    for parent in root.parents:
        catalog=parent/'catalog.json'
        if catalog.is_file():
            try:
                if read(catalog).get('organization')=='IndicStudio':lock=parent/'.android-build.lock';break
            except (ValueError,OSError):pass
    own={'pid':os.getpid(),'created':psutil.Process().create_time(),'product':root.name}
    for attempt in range(2):
        try:
            fd=os.open(lock,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
            with os.fdopen(fd,'w',encoding='utf-8') as f:json.dump(own,f)
            break
        except FileExistsError:
            try:
                prior=read(lock)
                p=psutil.Process(prior['pid'])
                if abs(p.create_time()-prior['created'])<.01:raise ValueError('Another owned IndicStudio build holds the build slot; finish it first')
            except psutil.NoSuchProcess:pass
            except (KeyError,json.JSONDecodeError):raise ValueError('Invalid build-slot record; inspect it without starting another build')
            lock.unlink()
    else:raise ValueError('Unable to acquire build slot')
    try:yield
    finally:
        if lock.exists() and read(lock)==own:lock.unlink()

def build_android(root,task='debug',rerun_tasks=False):
    with build_slot(root):_build_android(root,task,rerun_tasks)

def _build_android(root,task='debug',rerun_tasks=False):
    import psutil
    validate(root,quiet=True);project=read(root/'project.json');source=inside(root,'source_code')
    tasks={'debug':[':app:assembleDebug'],'check':[':app:testDebugUnitTest',':app:lintDebug'],'bundle':[':app:bundleRelease'],'help':['help']}
    if task=='check':
        settings=source/'settings.gradle.kts'
        text=settings.read_text(encoding='utf-8-sig') if settings.exists() else ''
        for module in ['shared','server']:
            if (source/module/'src/test').is_dir() and re.search(r'include\(\s*":'+module+r'"\s*\)',text):
                tasks['check'].append(':'+module+':test')
    if not (source/'gradlew.bat').exists():raise ValueError('This helper requires an Android Gradle project')
    env=os.environ.copy();java=env.get('JAVA_HOME')
    if not java and os.name=='nt':java=r'C:\Program Files\Android\Android Studio\jbr'
    sdk=env.get('ANDROID_HOME') or env.get('ANDROID_SDK_ROOT')
    if not sdk and os.name=='nt':sdk=str(Path.home()/'AppData/Local/Android/Sdk')
    if java:env['JAVA_HOME']=java;env['PATH']=str(Path(java)/'bin')+os.pathsep+env.get('PATH','')
    if sdk and Path(sdk).is_dir():
        env['ANDROID_HOME']=sdk;env['ANDROID_SDK_ROOT']=sdk
        local=source/'local.properties'
        if not local.exists():
            # Java properties require escaping the Windows drive colon even when
            # forward slashes avoid the usual backslash escaping problem.
            sdk_property=Path(sdk).as_posix().replace(':','\\:')
            local.write_text('sdk.dir='+sdk_property+'\n',encoding='utf-8',newline='\n')
    marker='indicstudio-'+project['slug']+'-'+str(os.getpid())
    env['JAVA_TOOL_OPTIONS']=(env.get('JAVA_TOOL_OPTIONS','')+' -Dindicstudio.run='+marker).strip()
    command=([os.environ.get('COMSPEC','cmd.exe'),'/d','/c','gradlew.bat'] if os.name=='nt' else ['sh','./gradlew'])+['--no-daemon','--max-workers=2']+tasks[task]
    if rerun_tasks:command.append('--rerun-tasks')
    logdir=root/'docs/verification';logdir.mkdir(exist_ok=True);log=logdir/f'local-{task}-{datetime.now().strftime("%Y%m%d-%H%M%S")}.log'
    owned={};stop=threading.Event();started=now();proc=None
    def remember(p):
        try:owned[p.pid]=p.create_time()
        except (psutil.NoSuchProcess,psutil.AccessDenied):pass
    def monitor():
        while not stop.wait(.5):
            if proc:
                try:
                    for child in psutil.Process(proc.pid).children(recursive=True):remember(child)
                except (psutil.NoSuchProcess,psutil.AccessDenied):pass
    with log.open('w',encoding='utf-8') as handle:
        proc=subprocess.Popen(command,cwd=source,env=env,stdout=handle,stderr=subprocess.STDOUT);remember(psutil.Process(proc.pid));watch=threading.Thread(target=monitor,daemon=True);watch.start()
        try:code=proc.wait(timeout=1800)
        finally:
            stop.set();watch.join(timeout=2)
            # Same PID and creation time only. No name-based stops or shared adb/Gradle stop commands.
            leftovers=[]
            for pid,created in reversed(list(owned.items())):
                try:
                    p=psutil.Process(pid)
                    if abs(p.create_time()-created)>.01:continue
                    try:p.wait(timeout=3)
                    except psutil.TimeoutExpired:
                        p.terminate()
                        try:p.wait(timeout=5)
                        except psutil.TimeoutExpired:p.kill();p.wait(timeout=5)
                except (psutil.NoSuchProcess,psutil.AccessDenied):continue
            for pid,created in owned.items():
                try:
                    p=psutil.Process(pid)
                    if abs(p.create_time()-created)<.01 and p.is_running():leftovers.append(pid)
                except psutil.NoSuchProcess:pass
            dump(log.with_suffix('.json'),{'product':project['slug'],'task':task,'started':started,'ended':now(),'controller_pid':proc.pid,'owned_process_identities':[{'pid':pid,'created':created} for pid,created in owned.items()],'owned_leftovers':leftovers,'log':log.relative_to(root).as_posix()})
    if code:raise ValueError(f'Build failed with exit code {code}; see {log.relative_to(root)}')
    if leftovers:raise ValueError('Build completed but an owned helper could not be reclaimed')
    print(json.dumps({'built':True,'product':project['slug'],'task':task,'log':str(log),'owned_leftovers':len(leftovers)}))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--project',type=Path,default=DEFAULT)
    commands=parser.add_subparsers(dest='command',required=True);validation=commands.add_parser('validate');validation.add_argument('--require-builds',action='store_true')
    package_cmd=commands.add_parser('package');package_cmd.add_argument('--destination',type=Path,required=True)
    release_cmd=commands.add_parser('stage-release');release_cmd.add_argument('release_id');release_cmd.add_argument('--apk',type=Path);release_cmd.add_argument('--aab',type=Path);release_cmd.add_argument('--channel',choices=['preview','beta'],default='preview')
    release_cmd.add_argument('--artifact',action='append',default=[],help='Repeat OS=path for desktop/game installers or archives')
    sync=commands.add_parser('sync-website');sync.add_argument('--website',type=Path,required=True);sync.add_argument('--apply',action='store_true')
    build=commands.add_parser('build');build.add_argument('--task',choices=['debug','check','bundle','help'],default='debug')
    build.add_argument('--rerun-tasks',action='store_true',help='Explicitly rerun tasks when a confirmed stale result needs refreshing; preserves caches')
    args=parser.parse_args();root=args.project.resolve()
    if args.command=='validate':validate(root,require_builds=args.require_builds)
    elif args.command=='package':package(root,args.destination)
    elif args.command=='stage-release':
        extra=[]
        for item in args.artifact:
            if '=' not in item:raise ValueError('Use --artifact OS=path')
            os_name,path=item.split('=',1);extra.append((os_name,Path(path)))
        print(json.dumps({'staged':stage_release(root,args.release_id,args.apk,args.aab,args.channel,extra_artifacts=extra)['release_id']}))
    elif args.command=='sync-website':sync_website(root,args.website,args.apply)
    elif args.command=='build':build_android(root,args.task,args.rerun_tasks)
if __name__=='__main__':
    try:main()
    except (ValueError,FileNotFoundError) as exc:print(str(exc),file=sys.stderr);sys.exit(1)
