export type Control = 'toggle' | 'slider'
export type Feature = {
  id: string
  name: string
  desc: string
  key?: string
  control?: Control
  min?: number
  max?: number
  step?: number
  unit?: string
  def?: number
  log?: boolean
}
export type Category = { id: string; code: string; label: string; blurb: string; features: Feature[]; panel?: string }

export const CATEGORIES: Category[] = [
  {
    id: 'stat', code: '01', label: 'Statistik & Karakter', blurb: 'Ketahanan, sumber daya, dan atribut inti karakter.', panel: 'stats',
    features: [
      { id: 'god', name: 'God Mode / Invincibility', desc: 'Kebal terhadap segala bentuk serangan dan kerusakan lingkungan.', key: 'F1' },
      { id: 'hp', name: 'Infinite HP / Health', desc: 'Darah tidak pernah berkurang atau selalu otomatis terisi penuh.', key: 'F2' },
      { id: 'stam', name: 'Infinite Stamina', desc: 'Stamina tidak habis saat berlari, menghindar, atau menyerang.', key: 'F3' },
      { id: 'mana', name: 'Infinite Mana / MP / Energy', desc: 'Penggunaan sihir atau kemampuan khusus tanpa batas.', key: 'F4' },
      { id: 'poise', name: 'Infinite Poise / Super Armor', desc: 'Tidak pernah mengalami stagger, jatuh, atau terlempar.', key: 'F5' },
      { id: 'cd', name: 'No Cooldown', desc: 'Skill, sihir, dan item dapat digunakan tanpa jeda.', key: 'F6' },
      { id: 'ult', name: 'Instant Ultimate / Max Rage', desc: 'Mengisi penuh indikator serangan pamungkas secara instan.', key: 'F7' },
      { id: 'immune', name: 'Status Effect Immunity', desc: 'Kebal racun, api, beku, kutukan, dan stun.', key: 'F8' },
    ],
  },
  {
    id: 'combat', code: '02', label: 'Pertempuran & Damage', blurb: 'Output kerusakan, mekanik serangan, dan pertahanan otomatis.',
    features: [
      { id: 'ohk', name: 'One-Hit Kill', desc: 'Membunuh musuh atau bos dengan satu serangan.', key: 'NUM1' },
      { id: 'dmg', name: 'Damage Multiplier', desc: 'Mengalikan kerusakan fisik atau magis.', key: 'NUM2', control: 'slider', min: 2, max: 9999, unit: '×', def: 10, log: true },
      { id: 'crit', name: '100% Critical Hit Rate', desc: 'Setiap serangan yang mendarat selalu kritikal.', key: 'NUM3' },
      { id: 'critdmg', name: 'Maximum Critical Damage', desc: 'Pengganda kritikal diatur ke batas tertinggi.', key: 'NUM4' },
      { id: 'dura', name: 'Infinite Durability', desc: 'Senjata dan armor tidak pernah rusak atau tumpul.' },
      { id: 'gbreak', name: 'Always Guard Break', desc: 'Menghancurkan perisai atau posture musuh dalam satu serangan.' },
      { id: 'ammo', name: 'Infinite Ammo', desc: 'Peluru, anak panah, dan senjata lempar tidak berkurang.', key: 'NUM5' },
      { id: 'parry', name: 'Auto-Dodge / Perfect Parry', desc: 'Hindaran atau tangkisan sempurna dalam milidetik.', key: 'NUM6' },
      { id: 'aspd', name: 'Attack Speed Modifier', desc: 'Mempercepat ayunan dan memotong jeda animasi.', control: 'slider', min: 1, max: 5, step: 0.1, unit: '×', def: 1.5 },
      { id: 'reach', name: 'Extended Hitbox / Reach', desc: 'Memperbesar area tabrakan senjata.', control: 'slider', min: 1, max: 30, step: 0.5, unit: 'm', def: 4 },
    ],
  },
  {
    id: 'eco', code: '03', label: 'Ekonomi & Inventori', blurb: 'Mata uang, item, crafting, dan kapasitas tas.',
    features: [
      { id: 'gold', name: 'Infinite Gold / Currency', desc: 'Mata uang utama permainan tidak terbatas.', key: 'CTRL+G' },
      { id: 'gem', name: 'Infinite Premium Currency', desc: 'Kristal, gem, atau koin khusus tidak terbatas.' },
      { id: 'items', name: 'Infinite Items / Consumables', desc: 'Ramuan, buff, dan item lempar tidak berkurang.' },
      { id: 'craft', name: 'Zero Material Crafting', desc: 'Membuat senjata, zirah, dan ramuan tanpa bahan baku.' },
      { id: 'unlockeq', name: 'Unlock All Weapons & Equipment', desc: 'Membuka seluruh senjata, zirah, dan aksesori.' },
      { id: 'upg', name: 'Max Upgrade Level', desc: 'Perlengkapan langsung ke tingkat tertinggi.' },
      { id: 'weight', name: 'Unlimited Capacity', desc: 'Tas inventori tanpa batasan beban.' },
      { id: 'loot', name: 'Auto-Loot / Vacuum', desc: 'Mengisap item, material, dan koin di sekitar.', control: 'slider', min: 5, max: 200, unit: 'm', def: 25 },
    ],
  },
  {
    id: 'prog', code: '04', label: 'Progresi & Leveling', blurb: 'Pengalaman, poin, skill tree, dan reputasi.',
    features: [
      { id: 'exp', name: 'EXP Multiplier', desc: 'Perolehan pengalaman berkali-kali lipat.', control: 'slider', min: 1, max: 100, unit: '×', def: 5 },
      { id: 'sp', name: 'Infinite Skill / Attribute Points', desc: 'Poin skill tree dan atribut tanpa batas.' },
      { id: 'skills', name: 'Unlock All Skills', desc: 'Membuka seluruh cabang kemampuan dan perk.' },
      { id: 'mastery', name: 'Max Weapon Mastery', desc: 'Kemahiran senjata otomatis maksimal.' },
      { id: 'rep', name: 'Max Reputation / Faction Rank', desc: 'Reputasi faksi langsung di level tertinggi.' },
    ],
  },
  {
    id: 'world', code: '05', label: 'Pergerakan & Dunia', blurb: 'Navigasi, fisika, dan kondisi dunia.', panel: 'world',
    features: [
      { id: 'speed', name: 'Super Speed', desc: 'Mengatur kecepatan gerak berlari atau berjalan.', key: 'SHIFT+S', control: 'slider', min: 1, max: 10, step: 0.1, unit: '×', def: 2 },
      { id: 'noclip', name: 'No-Clip Mode', desc: 'Menembus dinding, pintu terkunci, dan batas peta.', key: 'SHIFT+N' },
      { id: 'fly', name: 'Infinite Jump / Fly Mode', desc: 'Melompat di udara atau terbang bebas.', key: 'SHIFT+F' },
      { id: 'fall', name: 'Disable Fall Damage', desc: 'Meniadakan kerusakan jatuh dari ketinggian.' },
      { id: 'quest', name: 'Ignore Quest Requirements', desc: 'Membuka pintu dan tugas tanpa memicu alur cerita.' },
    ],
  },
  {
    id: 'esp', code: '06', label: 'Visual, Kamera & ESP', blurb: 'Persepsi tambahan dan kontrol kamera.',
    features: [
      { id: 'lootesp', name: 'Loot / Item ESP', desc: 'Nama, jarak, dan warna kelangkaan item menembus dinding.', key: 'ALT+1' },
      { id: 'enemyesp', name: 'Enemy ESP / Chams', desc: 'Siluet, kerangka, atau posisi musuh di balik tembok.', key: 'ALT+2' },
      { id: 'freecam', name: 'Freecam / Unlocked Camera', desc: 'Lepas kamera dan jelajahi peta secara bebas.', key: 'ALT+3' },
      { id: 'fov', name: 'Custom FOV', desc: 'Jarak pandang melebihi batas asli game.', control: 'slider', min: 60, max: 150, unit: '°', def: 90 },
    ],
  },
  {
    id: 'ai', code: '07', label: 'Manipulasi Musuh', blurb: 'Perilaku AI, agresi, dan hasil jarahan.',
    features: [
      { id: 'dumb', name: "Dumb AI / Enemies Don't Attack", desc: 'Musuh dan bos hanya berdiam diri.' },
      { id: 'aggro', name: 'Invisible / Ignore Aggro', desc: 'Tidak terdeteksi musuh, bahkan dari dekat.' },
      { id: 'freeze', name: 'Freeze Enemies', desc: 'Menghentikan seluruh animasi musuh di tempat.', key: 'CTRL+F' },
      { id: 'onehp', name: 'Drain Enemy Health / 1 HP', desc: 'Darah musuh sekitar menjadi 1 poin.' },
      { id: 'drop', name: '100% Drop Rate', desc: 'Selalu menjatuhkan item kelangkaan tertinggi.' },
      { id: 'steal', name: 'Always Steal Success', desc: 'Peluang mencuri selalu 100% berhasil.' },
    ],
  },
  {
    id: 'sys', code: '08', label: 'Sistem & Data', blurb: 'Skala waktu, spawner, dan editor flag mentah.', panel: 'sys',
    features: [
      { id: 'timescale', name: 'Time Scale / Game Speed', desc: 'Slow motion atau fast forward seluruh dunia.', key: 'CTRL+T', control: 'slider', min: 0.1, max: 5, step: 0.05, unit: '×', def: 1 },
    ],
  },
]

export const ALL = CATEGORIES.flatMap((c) => c.features.map((f) => ({ ...f, cat: c.id })))
export type FState = Record<string, { on: boolean; v: number }>
export const init: FState = Object.fromEntries(ALL.map((f) => [f.id, { on: false, v: f.def ?? 0 }]))

export const fromLog = (p: number, min: number, max: number) => Math.round(min * Math.pow(max / min, p / 1000))
export const toLog = (v: number, min: number, max: number) => (Math.log(v / min) / Math.log(max / min)) * 1000
export const stamp = () => new Date().toLocaleTimeString('id-ID', { hour12: false })
