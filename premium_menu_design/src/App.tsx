import { useEffect, useMemo, useRef, useState, type ReactNode, type MouseEvent, type KeyboardEvent } from 'react'
import { motion, AnimatePresence } from 'motion/react'
import {
  Shield, Swords, Coins, TrendingUp, Compass, Eye, Brain, Cpu, Search, Command, Power, Activity,
  Zap, Gauge, Lock, ChevronRight, MapPin, Clock, Package, Flag, Plus, Minus, X, Radio, Minimize2,
  Sparkles, CornerDownLeft, Fingerprint, Layers, Sun, Cloud, CloudRain, CloudLightning, Snowflake,
  type LucideIcon,
} from 'lucide-react'
import { CATEGORIES, ALL, init, fromLog, toLog, stamp, type Feature, type FState } from './data'

const ICONS: Record<string, LucideIcon> = {
  stat: Shield, combat: Swords, eco: Coins, prog: TrendingUp, world: Compass, esp: Eye, ai: Brain, sys: Cpu,
}
const FKEYS: Record<string, string> = { F1: 'god', F2: 'hp', F3: 'stam', F4: 'mana', F5: 'poise', F6: 'cd', F7: 'ult', F8: 'immune' }

type LogEntry = { id: number; t: string; m: string; on?: boolean }
type Toast = { id: number; m: string; on?: boolean }
let uid = 0

/* ───────────────────────── primitives ───────────────────────── */

function Switch({ on, onClick, size = 'md' }: { on: boolean; onClick: () => void; size?: 'sm' | 'md' }) {
  const w = size === 'sm' ? 'h-5 w-9' : 'h-7 w-[52px]'
  const k = size === 'sm' ? 'h-3.5 w-3.5' : 'h-5 w-5'
  return (
    <button
      onClick={onClick}
      aria-pressed={on}
      className={`relative flex shrink-0 items-center rounded-full border p-[3px] transition-colors duration-300 ${w} ${
        on ? 'justify-end border-amber/70 bg-gradient-to-r from-amber/20 to-amber/40' : 'justify-start border-line bg-ink/80'
      }`}
    >
      <motion.span
        layout
        transition={{ type: 'spring', stiffness: 700, damping: 32 }}
        className={`rounded-full ${k} ${on ? 'bg-amber shadow-[0_0_14px_2px_rgba(255,176,0,0.7)]' : 'bg-dim/50'}`}
      />
    </button>
  )
}

function Glass({ children, className = '' }: { children: ReactNode; className?: string }) {
  return <div className={`glass hairline rounded-2xl ${className}`}>{children}</div>
}

function Label({ children, right }: { children: ReactNode; right?: ReactNode }) {
  return (
    <div className="mb-3 flex items-center justify-between font-mono text-[10px] tracking-[0.22em] text-dim uppercase">
      <span>{children}</span>
      {right}
    </div>
  )
}

function Spark({ data, color = '#ffb000', h = 28 }: { data: number[]; color?: string; h?: number }) {
  const max = Math.max(...data), min = Math.min(...data)
  const pts = data.map((d, i) => `${(i / (data.length - 1)) * 100},${h - ((d - min) / (max - min || 1)) * (h - 4) - 2}`).join(' ')
  const id = 'g' + color.slice(1)
  return (
    <svg viewBox={`0 0 100 ${h}`} preserveAspectRatio="none" className="h-7 w-full">
      <defs>
        <linearGradient id={id} x1="0" x2="0" y1="0" y2="1">
          <stop offset="0" stopColor={color} stopOpacity="0.35" />
          <stop offset="1" stopColor={color} stopOpacity="0" />
        </linearGradient>
      </defs>
      <polygon points={`0,${h} ${pts} 100,${h}`} fill={`url(#${id})`} />
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.4" vectorEffect="non-scaling-stroke" />
    </svg>
  )
}

function Ring({ value, label }: { value: number; label: string }) {
  const r = 54, c = 2 * Math.PI * r
  return (
    <div className="relative grid h-36 w-36 place-items-center">
      <svg viewBox="0 0 140 140" className="absolute inset-0 -rotate-90">
        <circle cx="70" cy="70" r={r} fill="none" stroke="#2f2d29" strokeWidth="2" />
        {Array.from({ length: 48 }).map((_, i) => (
          <line key={i} x1="70" y1="6" x2="70" y2={i % 4 ? 9 : 12} stroke="#3a3833" strokeWidth="1" transform={`rotate(${i * 7.5} 70 70)`} />
        ))}
        <motion.circle
          cx="70" cy="70" r={r} fill="none" stroke="url(#ringg)" strokeWidth="6" strokeLinecap="round"
          strokeDasharray={c} initial={false} animate={{ strokeDashoffset: c * (1 - value) }}
          transition={{ type: 'spring', stiffness: 60, damping: 16 }}
          style={{ filter: 'drop-shadow(0 0 8px rgba(255,176,0,0.6))' }}
        />
        <defs>
          <linearGradient id="ringg" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stopColor="#ffd36b" />
            <stop offset="1" stopColor="#ff7a1a" />
          </linearGradient>
        </defs>
      </svg>
      <div className="text-center">
        <div className="font-mono text-3xl font-bold tabular-nums">{Math.round(value * 100)}<span className="text-base text-dim">%</span></div>
        <div className="font-mono text-[9px] tracking-[0.25em] text-dim">{label}</div>
      </div>
    </div>
  )
}

function useLive(base: number, jitter: number) {
  const [d, setD] = useState(() => Array.from({ length: 32 }, () => base + (Math.random() - 0.5) * jitter))
  useEffect(() => {
    const id = setInterval(() => setD((p) => [...p.slice(1), base + (Math.random() - 0.5) * jitter]), 900)
    return () => clearInterval(id)
  }, [base, jitter])
  return d
}

/* ───────────────────────── feature card ───────────────────────── */

function FeatureCard({ f, s, onToggle, onVal }: { f: Feature & { cat: string }; s: { on: boolean; v: number }; onToggle: () => void; onVal: (v: number) => void }) {
  const Icon = ICONS[f.cat]
  const move = (e: MouseEvent<HTMLDivElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    e.currentTarget.style.setProperty('--x', `${e.clientX - r.left}px`)
    e.currentTarget.style.setProperty('--y', `${e.clientY - r.top}px`)
  }
  const pct = f.control === 'slider'
    ? f.log ? toLog(s.v, f.min!, f.max!) / 10 : ((s.v - f.min!) / (f.max! - f.min!)) * 100
    : 0
  return (
    <motion.div
      layout
      initial={{ opacity: 0, y: 12 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0, scale: 0.98 }}
      transition={{ duration: 0.25 }}
      onMouseMove={move}
      className={`group relative overflow-hidden rounded-xl border p-5 transition-all duration-300 ${
        s.on ? 'border-amber/40 bg-gradient-to-br from-amber/[0.09] via-raise/80 to-raise/60 shadow-[0_0_40px_-12px_rgba(255,176,0,0.45)]' : 'border-line/80 bg-panel/60 hover:border-line hover:bg-raise/60'
      }`}
    >
      <div
        className="pointer-events-none absolute inset-0 opacity-0 transition-opacity duration-300 group-hover:opacity-100"
        style={{ background: 'radial-gradient(320px circle at var(--x) var(--y), rgba(255,176,0,0.09), transparent 45%)' }}
      />
      <div className="relative flex items-start gap-4">
        <div className={`grid h-10 w-10 shrink-0 place-items-center rounded-lg border transition-colors ${s.on ? 'border-amber/50 bg-amber/15 text-amber' : 'border-line bg-ink/60 text-dim'}`}>
          <Icon size={17} strokeWidth={1.6} />
        </div>
        <div className="min-w-0 flex-1">
          <div className="flex flex-wrap items-center gap-2">
            <h3 className="text-[15px] font-medium tracking-tight">{f.name}</h3>
            {f.key && <kbd className="rounded border border-line bg-ink/60 px-1.5 py-px font-mono text-[9px] text-dim">{f.key}</kbd>}
          </div>
          <p className="mt-1 text-[12.5px] leading-relaxed text-dim">{f.desc}</p>
        </div>
        <Switch on={s.on} onClick={onToggle} />
      </div>
      {f.control === 'slider' && (
        <div className={`relative mt-5 transition-opacity ${s.on ? '' : 'opacity-35'}`}>
          <div className="mb-2 flex items-baseline justify-between">
            <span className="font-mono text-[10px] tracking-widest text-dim">{f.min}{f.unit}</span>
            <motion.span key={s.v} initial={{ opacity: 0.4, y: -2 }} animate={{ opacity: 1, y: 0 }} className="font-mono text-xl font-semibold text-amber tabular-nums">
              {s.v.toLocaleString('id-ID')}<span className="text-sm text-amber/60">{f.unit}</span>
            </motion.span>
            <span className="font-mono text-[10px] tracking-widest text-dim">{f.max!.toLocaleString('id-ID')}{f.unit}</span>
          </div>
          <input
            type="range" className="w-full"
            style={{ background: `linear-gradient(90deg, #ff7a1a, #ffb000 ${pct}%, #2f2d29 ${pct}%)` }}
            min={f.log ? 0 : f.min} max={f.log ? 1000 : f.max} step={f.log ? 1 : f.step ?? 1}
            value={f.log ? toLog(s.v, f.min!, f.max!) : s.v}
            onChange={(e) => onVal(f.log ? fromLog(+e.target.value, f.min!, f.max!) : +e.target.value)}
          />
        </div>
      )}
    </motion.div>
  )
}

/* ───────────────────────── app ───────────────────────── */

export default function App() {
  const [state, setState] = useState<FState>(init)
  const [cat, setCat] = useState('stat')
  const [filter, setFilter] = useState<'all' | 'on' | 'off'>('all')
  const [log, setLog] = useState<LogEntry[]>([{ id: uid++, t: stamp(), m: 'Sesi dimulai · injeksi berhasil' }])
  const [toasts, setToasts] = useState<Toast[]>([])
  const [profile, setProfile] = useState('Default')
  const [palette, setPalette] = useState(false)
  const [hidden, setHidden] = useState(false)
  const stateRef = useRef(state)
  stateRef.current = state

  const fps = useLive(144, 14)
  const lat = useLive(3.2, 1.4)
  const mem = useLive(820, 180)

  const push = (m: string, on?: boolean) => {
    const id = uid++
    setLog((l) => [{ id, t: stamp(), m, on }, ...l].slice(0, 50))
    setToasts((t) => [...t.slice(-2), { id, m, on }])
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 2400)
  }
  const toggleId = (id: string) => {
    const f = ALL.find((x) => x.id === id)!
    const on = !stateRef.current[id].on
    setState((s) => ({ ...s, [id]: { ...s[id], on } }))
    push(`${f.name} ${on ? 'diaktifkan' : 'dinonaktifkan'}`, on)
  }
  const setVal = (id: string, v: number) => setState((s) => ({ ...s, [id]: { ...s[id], v } }))
  const panic = () => { setState(init); push('PANIC · seluruh modul dimatikan', false) }
  const setCatAll = (on: boolean) => {
    setState((s) => {
      const n = { ...s }
      CATEGORIES.find((c) => c.id === cat)!.features.forEach((f) => (n[f.id] = { ...n[f.id], on }))
      return n
    })
    push(`${current.label}: semua ${on ? 'aktif' : 'nonaktif'}`, on)
  }

  useEffect(() => {
    const h = (e: globalThis.KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); setPalette((p) => !p); return }
      if (e.key === 'Insert') { setHidden((x) => !x); return }
      if (e.key === 'End') { panic(); return }
      if (FKEYS[e.key]) { e.preventDefault(); toggleId(FKEYS[e.key]) }
      if (e.key === 'Escape') setPalette(false)
    }
    window.addEventListener('keydown', h)
    return () => window.removeEventListener('keydown', h)
  }, [])

  const active = ALL.filter((f) => state[f.id].on)
  const countFor = (id: string) => ALL.filter((f) => f.cat === id && state[f.id].on).length
  const current = CATEGORIES.find((c) => c.id === cat)!
  const CatIcon = ICONS[cat]
  const list = useMemo(
    () => ALL.filter((f) => f.cat === cat && (filter === 'all' || (filter === 'on' ? state[f.id].on : !state[f.id].on))),
    [cat, filter, state],
  )
  const risk = Math.min(1, active.length / 30)
  const riskLabel = risk < 0.25 ? 'STEALTH' : risk < 0.6 ? 'ELEVATED' : 'EXPOSED'
  const riskColor = risk < 0.25 ? '#7bd88f' : risk < 0.6 ? '#ffb000' : '#ff5a36'

  return (
    <div className="relative h-screen overflow-hidden bg-ink text-bone selection:bg-amber selection:text-ink">
      {/* backdrop */}
      <div className="bg-grid pointer-events-none absolute inset-0" />
      <div className="pointer-events-none absolute -top-40 -left-40 h-[640px] w-[640px] rounded-full bg-amber/[0.07] blur-[120px]" />
      <div className="pointer-events-none absolute -right-40 -bottom-60 h-[560px] w-[560px] rounded-full bg-[#ff5a36]/[0.05] blur-[120px]" />
      <div className="scanline pointer-events-none absolute inset-x-0 top-0 h-40 bg-gradient-to-b from-transparent via-amber/[0.025] to-transparent" />

      <AnimatePresence>
        {hidden && (
          <motion.button
            initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 20 }}
            onClick={() => setHidden(false)}
            className="glass hairline absolute bottom-8 left-1/2 z-50 flex -translate-x-1/2 items-center gap-3 rounded-full px-5 py-3 font-mono text-xs"
          >
            <span className="h-2 w-2 animate-pulse rounded-full bg-amber" />
            OVERRIDE berjalan di latar · {active.length} modul aktif
            <kbd className="rounded border border-line px-1.5 text-[10px] text-dim">INS</kbd>
          </motion.button>
        )}
      </AnimatePresence>

      <motion.div
        animate={hidden ? { opacity: 0, scale: 0.97, filter: 'blur(8px)' } : { opacity: 1, scale: 1, filter: 'blur(0px)' }}
        transition={{ duration: 0.35 }}
        className={`relative flex h-full gap-3 p-3 ${hidden ? 'pointer-events-none' : ''}`}
      >
        {/* ── rail ── */}
        <Glass className="flex w-[264px] shrink-0 flex-col overflow-hidden">
          <div className="flex items-center gap-3 px-5 pt-6 pb-5">
            <div className="relative grid h-11 w-11 place-items-center">
              <motion.div
                animate={{ rotate: 360 }} transition={{ repeat: Infinity, duration: 8, ease: 'linear' }}
                className="absolute inset-0 rounded-xl"
                style={{ background: 'conic-gradient(from 0deg, #ffb000, transparent 40%, #ff7a1a 70%, transparent)' }}
              />
              <div className="absolute inset-[1.5px] rounded-[10px] bg-ink" />
              <span className="relative font-mono text-lg font-bold text-amber">Ω</span>
            </div>
            <div>
              <div className="text-[17px] font-semibold tracking-tight">OVERRIDE</div>
              <div className="font-mono text-[9px] tracking-[0.3em] text-amber/80">ENTERPRISE · v4.12</div>
            </div>
          </div>

          <div className="px-3">
            <button
              onClick={() => setPalette(true)}
              className="flex w-full items-center gap-2.5 rounded-lg border border-line bg-ink/50 px-3 py-2 text-left text-[13px] text-dim transition-colors hover:border-amber/40 hover:text-bone"
            >
              <Search size={14} />
              <span className="flex-1">Cari modul…</span>
              <kbd className="flex items-center gap-0.5 rounded border border-line px-1.5 py-px font-mono text-[10px]"><Command size={9} />K</kbd>
            </button>
          </div>

          <nav className="mt-4 flex-1 overflow-y-auto px-3 pb-3">
            <div className="mb-2 px-2 font-mono text-[9px] tracking-[0.3em] text-dim/70">MODUL</div>
            {CATEGORIES.map((c) => {
              const n = countFor(c.id), sel = c.id === cat, I = ICONS[c.id]
              return (
                <button key={c.id} onClick={() => setCat(c.id)} className="relative mb-0.5 flex w-full items-center gap-3 rounded-lg px-3 py-2.5 text-left">
                  {sel && (
                    <motion.div layoutId="navpill" transition={{ type: 'spring', stiffness: 500, damping: 38 }}
                      className="absolute inset-0 rounded-lg border border-amber/30 bg-gradient-to-r from-amber/15 to-transparent" />
                  )}
                  <I size={16} strokeWidth={1.7} className={`relative ${sel ? 'text-amber' : 'text-dim'}`} />
                  <div className="relative flex-1">
                    <div className={`text-[13px] ${sel ? 'text-bone' : 'text-dim'}`}>{c.label}</div>
                    <div className="mt-1.5 h-[2px] overflow-hidden rounded-full bg-line/70">
                      <motion.div className="h-full bg-amber" animate={{ width: `${(n / c.features.length) * 100}%` }} />
                    </div>
                  </div>
                  <span className={`relative font-mono text-[10px] tabular-nums ${n ? 'text-amber' : 'text-dim/60'}`}>{n}/{c.features.length}</span>
                </button>
              )
            })}
          </nav>

          <div className="border-t border-line/70 p-4">
            <Label>Profil preset</Label>
            <div className="relative grid grid-cols-3 rounded-lg border border-line bg-ink/50 p-0.5">
              {['Default', 'Boss', 'Farm'].map((p) => (
                <button key={p} onClick={() => { setProfile(p); push(`Profil "${p}" dimuat`) }} className="relative py-1.5 text-[11px]">
                  {profile === p && <motion.div layoutId="prof" className="absolute inset-0 rounded-md bg-bone" />}
                  <span className={`relative ${profile === p ? 'font-medium text-ink' : 'text-dim'}`}>{p}</span>
                </button>
              ))}
            </div>
            <div className="mt-4 flex items-center gap-3 rounded-xl border border-line bg-gradient-to-br from-amber/10 to-transparent p-3">
              <Fingerprint size={20} className="text-amber" strokeWidth={1.5} />
              <div className="min-w-0 flex-1">
                <div className="text-[12px] font-medium">Lisensi Enterprise</div>
                <div className="font-mono text-[10px] text-dim">Seat 04/50 · HWID terkunci</div>
              </div>
              <Lock size={12} className="text-dim" />
            </div>
          </div>
        </Glass>

        {/* ── main ── */}
        <div className="flex min-w-0 flex-1 flex-col gap-3">
          <Glass className="flex items-center gap-3 px-4 py-3">
            <div className="flex items-center gap-2 rounded-full border border-[#7bd88f]/30 bg-[#7bd88f]/10 px-3 py-1.5 font-mono text-[11px]">
              <span className="relative flex h-2 w-2">
                <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-[#7bd88f] opacity-70" />
                <span className="relative inline-flex h-2 w-2 rounded-full bg-[#7bd88f]" />
              </span>
              <span className="text-[#7bd88f]">ATTACHED</span>
              <span className="text-bone">game.exe</span>
              <span className="text-dim">· PID 18842</span>
            </div>
            <div className="flex items-center gap-2 rounded-full border border-line bg-ink/40 px-3 py-1.5 font-mono text-[11px]">
              <Radio size={12} className="text-dim" />
              <span className="text-dim">HOOK</span>
              <span className="tabular-nums">{lat[lat.length - 1].toFixed(1)}ms</span>
            </div>
            <div className="flex items-center gap-2 rounded-full border px-3 py-1.5 font-mono text-[11px]" style={{ borderColor: riskColor + '55', color: riskColor, background: riskColor + '12' }}>
              <Shield size={12} />
              {riskLabel}
            </div>
            <div className="flex-1" />
            <button onClick={() => setHidden(true)} className="flex items-center gap-2 rounded-lg border border-line px-3 py-2 font-mono text-[11px] text-dim transition-colors hover:text-bone">
              <Minimize2 size={13} /> Sembunyikan <kbd className="text-[10px]">INS</kbd>
            </button>
            <button
              onClick={panic}
              className="group flex items-center gap-2 rounded-lg border border-alert/60 bg-alert/10 px-4 py-2 font-mono text-[11px] tracking-widest text-alert transition-all hover:bg-alert hover:text-ink hover:shadow-[0_0_24px_rgba(255,90,54,0.6)]"
            >
              <Power size={13} /> PANIC <kbd className="text-[10px] opacity-70">END</kbd>
            </button>
          </Glass>

          <div className="flex-1 overflow-y-auto pr-1">
            {/* hero */}
            <div className="grid grid-cols-12 gap-3">
              <Glass className="col-span-12 flex items-center gap-6 p-5 2xl:col-span-6">
                <Ring value={active.length / ALL.length} label="OVERRIDE LOAD" />
                <div className="flex-1">
                  <div className="font-mono text-[10px] tracking-[0.25em] text-amber">SESI AKTIF</div>
                  <div className="mt-1 text-2xl font-semibold tracking-tight">
                    <span className="tabular-nums">{active.length}</span> <span className="text-dim">dari {ALL.length} modul</span>
                  </div>
                  <div className="mt-4">
                    <div className="mb-1.5 flex justify-between font-mono text-[10px]">
                      <span className="text-dim">RISIKO DETEKSI</span>
                      <span style={{ color: riskColor }}>{Math.round(risk * 100)}%</span>
                    </div>
                    <div className="flex gap-[3px]">
                      {Array.from({ length: 24 }).map((_, i) => (
                        <div key={i} className="h-2 flex-1 rounded-[1px] transition-colors duration-500"
                          style={{ background: i / 24 < risk ? (i < 6 ? '#7bd88f' : i < 14 ? '#ffb000' : '#ff5a36') : '#2f2d29' }} />
                      ))}
                    </div>
                  </div>
                </div>
              </Glass>
              {[
                { l: 'FRAME RATE', v: fps, f: (n: number) => n.toFixed(0), u: 'fps', c: '#ffb000', I: Activity },
                { l: 'HOOK LATENCY', v: lat, f: (n: number) => n.toFixed(1), u: 'ms', c: '#6aa7ff', I: Zap },
                { l: 'MEM WRITES', v: mem, f: (n: number) => n.toFixed(0), u: '/s', c: '#7bd88f', I: Layers },
              ].map(({ l, v, f, u, c, I }) => (
                <Glass key={l} className="col-span-4 flex flex-col justify-between p-5 2xl:col-span-2">
                  <div className="flex items-center justify-between">
                    <span className="font-mono text-[10px] tracking-[0.22em] text-dim">{l}</span>
                    <I size={14} style={{ color: c }} />
                  </div>
                  <div className="mt-3 font-mono text-3xl font-semibold tabular-nums">
                    {f(v[v.length - 1])}<span className="ml-1 text-sm text-dim">{u}</span>
                  </div>
                  <div className="mt-2"><Spark data={v} color={c} /></div>
                </Glass>
              ))}
            </div>

            {/* category header */}
            <div className="mt-8 mb-5 flex flex-wrap items-end justify-between gap-4 px-1">
              <AnimatePresence mode="wait">
                <motion.div key={cat} initial={{ opacity: 0, x: -10 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: 10 }} className="flex items-center gap-4">
                  <div className="grid h-14 w-14 place-items-center rounded-2xl border border-amber/40 bg-gradient-to-br from-amber/25 to-amber/5 text-amber shadow-[0_0_30px_-6px_rgba(255,176,0,0.5)]">
                    <CatIcon size={24} strokeWidth={1.5} />
                  </div>
                  <div>
                    <div className="font-mono text-[10px] tracking-[0.3em] text-amber">MODUL {current.code} · {countFor(cat)}/{current.features.length} AKTIF</div>
                    <h1 className="mt-1 text-[34px] leading-none font-semibold tracking-tight">{current.label}</h1>
                    <p className="mt-2 text-[13px] text-dim">{current.blurb}</p>
                  </div>
                </motion.div>
              </AnimatePresence>
              <div className="flex items-center gap-2">
                <div className="relative flex rounded-lg border border-line bg-panel/70 p-0.5">
                  {(['all', 'on', 'off'] as const).map((k) => (
                    <button key={k} onClick={() => setFilter(k)} className="relative px-3 py-1.5 text-[12px]">
                      {filter === k && <motion.div layoutId="flt" className="absolute inset-0 rounded-md bg-raise ring-1 ring-amber/30" />}
                      <span className={`relative ${filter === k ? 'text-bone' : 'text-dim'}`}>{k === 'all' ? 'Semua' : k === 'on' ? 'Aktif' : 'Nonaktif'}</span>
                    </button>
                  ))}
                </div>
                <button onClick={() => setCatAll(true)} className="flex items-center gap-1.5 rounded-lg bg-gradient-to-b from-[#ffc53d] to-amber px-3.5 py-2 text-[12px] font-semibold text-ink shadow-[0_6px_20px_-6px_rgba(255,176,0,0.7)] transition-transform hover:-translate-y-px">
                  <Sparkles size={13} /> Aktifkan semua
                </button>
                <button onClick={() => setCatAll(false)} className="rounded-lg border border-line px-3 py-2 text-[12px] text-dim hover:text-bone">Reset</button>
              </div>
            </div>

            <motion.div layout className="grid grid-cols-1 gap-3 xl:grid-cols-2">
              <AnimatePresence mode="popLayout">
                {list.map((f) => (
                  <FeatureCard key={f.id} f={f} s={state[f.id]} onToggle={() => toggleId(f.id)} onVal={(v) => setVal(f.id, v)} />
                ))}
              </AnimatePresence>
            </motion.div>
            {list.length === 0 && <div className="rounded-xl border border-dashed border-line p-10 text-center text-sm text-dim">Tidak ada modul pada filter ini.</div>}

            <AnimatePresence mode="wait">
              <motion.div key={cat + 'p'} initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0 }}>
                {current.panel === 'stats' && <StatEditor push={push} />}
                {current.panel === 'world' && <WorldPanel push={push} />}
                {current.panel === 'sys' && <SysPanel push={push} />}
              </motion.div>
            </AnimatePresence>
            <div className="h-6" />
          </div>
        </div>

        {/* ── inspector ── */}
        <Glass className="hidden w-[300px] shrink-0 flex-col overflow-hidden lg:flex">
          <div className="border-b border-line/70 p-5">
            <Label right={<span className="text-amber">LIVE</span>}>Telemetri karakter</Label>
            <div className="mb-4 flex items-center gap-3">
              <div className="grid h-12 w-12 place-items-center rounded-xl border border-line bg-gradient-to-br from-raise to-ink font-mono text-sm text-amber">LV</div>
              <div>
                <div className="text-[14px] font-medium">Aurelian the Ashen</div>
                <div className="font-mono text-[10px] text-dim">Level {state.exp.on ? 'MAX · 99' : '47'} · Spellblade</div>
              </div>
            </div>
            {[
              ['HP', state.hp.on || state.god.on, '#ff5a36', 0.64],
              ['STAMINA', state.stam.on, '#7bd88f', 0.41],
              ['MANA', state.mana.on, '#6aa7ff', 0.78],
              ['RAGE', state.ult.on, '#ffb000', 0.22],
            ].map(([l, full, c, base]) => (
              <div key={l as string} className="mb-3">
                <div className="mb-1 flex justify-between font-mono text-[10px]">
                  <span className="text-dim">{l as string}</span>
                  <span style={{ color: full ? (c as string) : undefined }}>{full ? '∞ LOCKED' : `${Math.round((base as number) * 100)}%`}</span>
                </div>
                <div className="h-1.5 overflow-hidden rounded-full bg-ink">
                  <motion.div
                    className="h-full rounded-full"
                    animate={{ width: full ? '100%' : `${(base as number) * 100}%` }}
                    transition={{ type: 'spring', stiffness: 80, damping: 18 }}
                    style={{ background: `linear-gradient(90deg, ${c}88, ${c})`, boxShadow: full ? `0 0 12px ${c}` : 'none' }}
                  />
                </div>
              </div>
            ))}
          </div>

          <div className="border-b border-line/70 p-5">
            <Label right={<span className="text-amber tabular-nums">{active.length}</span>}>Modul aktif</Label>
            <div className="flex max-h-36 flex-wrap gap-1.5 overflow-y-auto">
              {active.length === 0 && <span className="text-xs text-dim">Belum ada modul aktif. Tekan F1–F8.</span>}
              <AnimatePresence>
                {active.map((f) => (
                  <motion.button
                    layout key={f.id} initial={{ opacity: 0, scale: 0.8 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: 0.8 }}
                    onClick={() => toggleId(f.id)}
                    className="group flex items-center gap-1 rounded-md border border-amber/30 bg-amber/10 px-2 py-0.5 font-mono text-[10px] text-amber hover:border-alert/60 hover:bg-alert/10 hover:text-alert"
                  >
                    {f.name.split(' /')[0]} <X size={10} className="opacity-50 group-hover:opacity-100" />
                  </motion.button>
                ))}
              </AnimatePresence>
            </div>
          </div>

          <div className="flex min-h-0 flex-1 flex-col p-5">
            <Label right={<button onClick={() => setLog([])} className="hover:text-bone">CLEAR</button>}>Log aktivitas</Label>
            <ol className="relative flex-1 overflow-y-auto font-mono text-[11px]">
              <AnimatePresence initial={false}>
                {log.map((e) => (
                  <motion.li key={e.id} initial={{ opacity: 0, x: 12 }} animate={{ opacity: 1, x: 0 }} className="relative flex gap-3 border-l border-line/80 py-1.5 pl-3">
                    <span className={`absolute top-[11px] -left-[3px] h-[5px] w-[5px] rounded-full ${e.on === true ? 'bg-amber' : e.on === false ? 'bg-alert' : 'bg-dim'}`} />
                    <span className="text-dim/70">{e.t}</span>
                    <span className={e.on === true ? 'text-bone' : 'text-dim'}>{e.m}</span>
                  </motion.li>
                ))}
              </AnimatePresence>
            </ol>
          </div>
        </Glass>
      </motion.div>

      {/* toasts */}
      <div className="pointer-events-none fixed right-6 bottom-6 z-50 flex flex-col items-end gap-2">
        <AnimatePresence>
          {toasts.map((t) => (
            <motion.div
              key={t.id} layout
              initial={{ opacity: 0, y: 16, scale: 0.95 }} animate={{ opacity: 1, y: 0, scale: 1 }} exit={{ opacity: 0, x: 40 }}
              className="glass hairline flex items-center gap-3 rounded-xl px-4 py-3 text-[13px] shadow-2xl"
            >
              <span className={`grid h-6 w-6 place-items-center rounded-md ${t.on === false ? 'bg-alert/15 text-alert' : 'bg-amber/15 text-amber'}`}>
                {t.on === false ? <Power size={12} /> : <Zap size={12} />}
              </span>
              {t.m}
            </motion.div>
          ))}
        </AnimatePresence>
      </div>

      <AnimatePresence>
        {palette && <Palette state={state} onClose={() => setPalette(false)} onToggle={toggleId} onGo={(c) => { setCat(c); setPalette(false) }} />}
      </AnimatePresence>
    </div>
  )
}

/* ───────────────────────── command palette ───────────────────────── */

function Palette({ state, onClose, onToggle, onGo }: { state: FState; onClose: () => void; onToggle: (id: string) => void; onGo: (c: string) => void }) {
  const [q, setQ] = useState('')
  const [i, setI] = useState(0)
  const res = useMemo(() => {
    const s = q.toLowerCase()
    return ALL.filter((f) => (f.name + ' ' + f.desc).toLowerCase().includes(s)).slice(0, 9)
  }, [q])
  useEffect(() => setI(0), [q])
  const key = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'ArrowDown') { e.preventDefault(); setI((x) => Math.min(res.length - 1, x + 1)) }
    if (e.key === 'ArrowUp') { e.preventDefault(); setI((x) => Math.max(0, x - 1)) }
    if (e.key === 'Enter' && res[i]) onToggle(res[i].id)
  }
  return (
    <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} onClick={onClose}
      className="fixed inset-0 z-[60] flex items-start justify-center bg-ink/70 pt-[14vh] backdrop-blur-sm">
      <motion.div
        initial={{ opacity: 0, y: -16, scale: 0.97 }} animate={{ opacity: 1, y: 0, scale: 1 }} exit={{ opacity: 0, y: -10, scale: 0.98 }}
        transition={{ type: 'spring', stiffness: 400, damping: 30 }}
        onClick={(e) => e.stopPropagation()}
        className="glass hairline w-full max-w-xl overflow-hidden rounded-2xl shadow-[0_40px_120px_-20px_rgba(0,0,0,0.9)]"
      >
        <div className="flex items-center gap-3 border-b border-line px-5 py-4">
          <Search size={16} className="text-amber" />
          <input autoFocus value={q} onChange={(e) => setQ(e.target.value)} onKeyDown={key}
            placeholder={`Cari & jalankan dari ${ALL.length} modul…`} className="flex-1 bg-transparent text-[15px] outline-none placeholder:text-dim" />
          <kbd className="rounded border border-line px-1.5 py-px font-mono text-[10px] text-dim">ESC</kbd>
        </div>
        <ul className="max-h-[420px] overflow-y-auto p-2">
          {res.map((f, idx) => {
            const I = ICONS[f.cat], on = state[f.id].on
            return (
              <li key={f.id} onMouseEnter={() => setI(idx)}
                className={`flex cursor-pointer items-center gap-3 rounded-lg px-3 py-2.5 ${idx === i ? 'bg-amber/10' : ''}`}
                onClick={() => onToggle(f.id)}>
                <I size={15} className={on ? 'text-amber' : 'text-dim'} />
                <div className="min-w-0 flex-1">
                  <div className="truncate text-[13px]">{f.name}</div>
                  <div className="truncate text-[11px] text-dim">{CATEGORIES.find((c) => c.id === f.cat)!.label}</div>
                </div>
                <span className={`rounded px-1.5 py-px font-mono text-[9px] ${on ? 'bg-amber/15 text-amber' : 'bg-line/60 text-dim'}`}>{on ? 'ON' : 'OFF'}</span>
                <button onClick={(e) => { e.stopPropagation(); onGo(f.cat) }} className="text-dim hover:text-bone"><ChevronRight size={14} /></button>
              </li>
            )
          })}
          {res.length === 0 && <li className="p-6 text-center text-sm text-dim">Tidak ditemukan.</li>}
        </ul>
        <div className="flex items-center gap-4 border-t border-line px-5 py-2.5 font-mono text-[10px] text-dim">
          <span className="flex items-center gap-1"><CornerDownLeft size={10} /> toggle</span>
          <span>↑↓ navigasi</span>
          <span className="flex items-center gap-1"><ChevronRight size={10} /> buka kategori</span>
        </div>
      </motion.div>
    </motion.div>
  )
}

/* ───────────────────────── special panels ───────────────────────── */

type P = { push: (m: string, on?: boolean) => void }

function Panel({ title, code, icon: I, children }: { title: string; code: string; icon: LucideIcon; children: ReactNode }) {
  return (
    <Glass className="mt-3 overflow-hidden">
      <div className="flex items-center gap-3 border-b border-line/70 px-5 py-3.5">
        <I size={15} className="text-amber" strokeWidth={1.7} />
        <h2 className="flex-1 text-[14px] font-medium">{title}</h2>
        <span className="rounded border border-line px-2 py-0.5 font-mono text-[9px] tracking-widest text-dim">{code}</span>
      </div>
      <div className="p-5">{children}</div>
    </Glass>
  )
}

function StatEditor({ push }: P) {
  const [st, setSt] = useState<Record<string, number>>({ Strength: 42, Agility: 35, Dexterity: 28, Vitality: 50, Intelligence: 19 })
  const MAX = 99
  const keys = Object.keys(st)
  const pt = (k: string, i: number, scale = 1) => {
    const a = (Math.PI * 2 * i) / keys.length - Math.PI / 2
    const r = 70 * scale * (k ? st[k] / MAX : 1)
    return `${90 + Math.cos(a) * r},${90 + Math.sin(a) * r}`
  }
  return (
    <Panel title="Custom Stat Editor" code="ATTR · RAW" icon={Gauge}>
      <div className="flex flex-col items-center gap-8 md:flex-row">
        <svg viewBox="0 0 180 180" className="h-48 w-48 shrink-0">
          {[0.25, 0.5, 0.75, 1].map((s) => (
            <polygon key={s} points={keys.map((_, i) => pt('', i, s)).join(' ')} fill="none" stroke="#2f2d29" />
          ))}
          {keys.map((_, i) => <line key={i} x1="90" y1="90" x2={pt('', i).split(',')[0]} y2={pt('', i).split(',')[1]} stroke="#2f2d29" />)}
          <motion.polygon animate={{ points: keys.map((k, i) => pt(k, i)).join(' ') }} fill="rgba(255,176,0,0.18)" stroke="#ffb000" strokeWidth="1.5"
            style={{ filter: 'drop-shadow(0 0 6px rgba(255,176,0,0.5))' }} />
          {keys.map((k, i) => {
            const [x, y] = pt('', i, 1.22).split(',')
            return <text key={k} x={x} y={y} fill="#8f887b" fontSize="8" fontFamily="JetBrains Mono" textAnchor="middle" dominantBaseline="middle">{k.slice(0, 3).toUpperCase()}</text>
          })}
        </svg>
        <div className="w-full flex-1 space-y-3">
          {keys.map((k) => {
            const v = st[k]
            return (
              <div key={k} className="flex items-center gap-4">
                <span className="w-24 text-[13px]">{k}</span>
                <div className="relative h-1.5 flex-1 overflow-hidden rounded-full bg-ink">
                  <motion.div className="h-full rounded-full bg-gradient-to-r from-[#ff7a1a] to-amber" animate={{ width: `${(v / MAX) * 100}%` }} />
                </div>
                <div className="flex items-center rounded-lg border border-line bg-ink/50 font-mono text-sm">
                  <button className="px-2 py-1 text-dim hover:text-amber" onClick={() => setSt({ ...st, [k]: Math.max(1, v - 1) })}><Minus size={12} /></button>
                  <input value={v} onChange={(e) => setSt({ ...st, [k]: Math.min(MAX, Math.max(1, +e.target.value || 1)) })} className="w-9 bg-transparent text-center tabular-nums outline-none" />
                  <button className="px-2 py-1 text-dim hover:text-amber" onClick={() => setSt({ ...st, [k]: Math.min(MAX, v + 1) })}><Plus size={12} /></button>
                </div>
              </div>
            )
          })}
          <div className="flex gap-2 pt-2">
            <button onClick={() => { setSt(Object.fromEntries(keys.map((k) => [k, 99]))); push('Semua atribut → 99', true) }} className="rounded-lg border border-line px-3 py-1.5 text-xs hover:border-amber/50">Max semua</button>
            <button onClick={() => push('Atribut ditulis ke memori', true)} className="rounded-lg bg-gradient-to-b from-[#ffc53d] to-amber px-3.5 py-1.5 text-xs font-semibold text-ink">Terapkan ke memori</button>
          </div>
        </div>
      </div>
    </Panel>
  )
}

function WorldPanel({ push }: P) {
  const wps: [string, number, number, string][] = [
    ['Gerbang Utara', 30, 22, '1204.5, 88.0, -330.2'],
    ['Katedral Abu', 64, 38, '-512.9, 140.3, 977.1'],
    ['Sarang Naga', 78, 70, '2210.0, 402.7, 15.6'],
    ['Pasar Pelabuhan', 22, 74, '-88.4, 12.0, -1440.8'],
  ]
  const [sel, setSel] = useState(0)
  const [xyz, setXyz] = useState(['0', '0', '0'])
  const [weather, setWeather] = useState('Cerah')
  const [frozen, setFrozen] = useState(false)
  const [hour, setHour] = useState(12)
  const W: [string, LucideIcon][] = [['Cerah', Sun], ['Berawan', Cloud], ['Hujan', CloudRain], ['Badai', CloudLightning], ['Salju', Snowflake]]
  return (
    <div className="grid gap-3 xl:grid-cols-2">
      <Panel title="Teleportation" code="WARP" icon={MapPin}>
        <div className="relative mb-4 aspect-[16/9] overflow-hidden rounded-xl border border-line bg-ink">
          <div className="bg-grid absolute inset-0 opacity-60" />
          <svg className="absolute inset-0 h-full w-full" viewBox="0 0 100 100" preserveAspectRatio="none">
            <path d="M5,60 C20,40 35,70 50,50 S80,30 95,45" stroke="#2f2d29" fill="none" strokeWidth="0.6" />
            <path d="M10,85 C30,75 50,90 70,78 S90,80 98,70" stroke="#2f2d29" fill="none" strokeWidth="0.6" />
          </svg>
          {wps.map(([n, x, y], i) => (
            <button key={n} onClick={() => setSel(i)} className="absolute -translate-x-1/2 -translate-y-1/2" style={{ left: `${x}%`, top: `${y}%` }}>
              {sel === i && <span className="absolute inset-0 -m-2 animate-ping rounded-full border border-amber" />}
              <span className={`block h-3 w-3 rotate-45 border ${sel === i ? 'border-amber bg-amber shadow-[0_0_12px_#ffb000]' : 'border-dim bg-ink'}`} />
            </button>
          ))}
          <div className="absolute bottom-2 left-3 font-mono text-[10px] text-dim">{wps[sel][3]}</div>
        </div>
        <div className="flex items-center justify-between rounded-lg border border-line bg-ink/40 px-3 py-2">
          <div>
            <div className="text-[13px]">{wps[sel][0]}</div>
            <div className="font-mono text-[10px] text-dim">waypoint #{sel + 1}</div>
          </div>
          <button onClick={() => push(`Teleport → ${wps[sel][0]}`, true)} className="rounded-md bg-gradient-to-b from-[#ffc53d] to-amber px-3 py-1.5 text-[11px] font-semibold text-ink">Warp</button>
        </div>
        <div className="mt-3 flex gap-2">
          {['X', 'Y', 'Z'].map((a, i) => (
            <label key={a} className="flex flex-1 items-center rounded-lg border border-line bg-ink/40 px-2.5 font-mono text-xs focus-within:border-amber/50">
              <span className="text-amber/70">{a}</span>
              <input value={xyz[i]} onChange={(e) => setXyz(xyz.map((v, j) => (j === i ? e.target.value : v)))} className="w-full bg-transparent px-2 py-2 outline-none" />
            </label>
          ))}
          <button onClick={() => push(`Teleport → (${xyz.join(', ')})`, true)} className="rounded-lg border border-amber/50 px-3 text-xs text-amber hover:bg-amber/10">Go</button>
        </div>
      </Panel>
      <Panel title="Freeze Game Time / Alter Weather" code="ENV" icon={Clock}>
        <div className="relative mb-5 grid h-28 place-items-center overflow-hidden rounded-xl border border-line"
          style={{ background: `linear-gradient(180deg, ${hour < 6 || hour > 19 ? '#0b0d1a' : hour < 9 || hour > 16 ? '#3a1f12' : '#1d2b3a'}, #121110)` }}>
          <motion.div
            className="absolute h-8 w-8 rounded-full"
            animate={{ left: `${(hour / 23) * 88 + 4}%`, top: `${70 - Math.sin((hour / 23) * Math.PI) * 55}%` }}
            style={{ background: hour < 6 || hour > 19 ? '#d8d4c8' : '#ffb000', boxShadow: `0 0 30px ${hour < 6 || hour > 19 ? '#d8d4c8' : '#ffb000'}` }}
          />
          <div className="relative font-mono text-3xl font-semibold tabular-nums">
            {String(hour).padStart(2, '0')}:00
            {frozen && <span className="ml-2 rounded bg-[#6aa7ff]/20 px-1.5 align-middle text-[10px] text-[#6aa7ff]">FROZEN</span>}
          </div>
        </div>
        <input type="range" min={0} max={23} value={hour} onChange={(e) => setHour(+e.target.value)} className="w-full"
          style={{ background: `linear-gradient(90deg, #ff7a1a, #ffb000 ${(hour / 23) * 100}%, #2f2d29 ${(hour / 23) * 100}%)` }} />
        <div className="mt-5 grid grid-cols-5 gap-1.5">
          {W.map(([w, I]) => (
            <button key={w} onClick={() => { setWeather(w); push(`Cuaca → ${w}`, true) }}
              className={`flex flex-col items-center gap-1.5 rounded-lg border py-2.5 text-[10px] transition-colors ${weather === w ? 'border-amber/50 bg-amber/10 text-amber' : 'border-line text-dim hover:text-bone'}`}>
              <I size={16} strokeWidth={1.6} />{w}
            </button>
          ))}
        </div>
        <div className="mt-5 flex items-center justify-between rounded-lg border border-line bg-ink/40 px-3 py-2.5">
          <span className="text-[13px]">Bekukan waktu dunia</span>
          <Switch size="sm" on={frozen} onClick={() => { setFrozen(!frozen); push(`Waktu ${!frozen ? 'dibekukan' : 'berjalan'}`, !frozen) }} />
        </div>
      </Panel>
    </div>
  )
}

function SysPanel({ push }: P) {
  const [type, setType] = useState<'Item' | 'NPC'>('Item')
  const [id, setId] = useState('0x00A1F3')
  const [qty, setQty] = useState(1)
  const [recent, setRecent] = useState<string[]>([])
  const [flags, setFlags] = useState([
    { k: 'Q_MAIN_014_BRIDGE', d: 'Jembatan kota runtuh', v: true },
    { k: 'NPC_ELARA_ALIVE', d: 'Elara si pandai besi hidup', v: false },
    { k: 'DLG_KING_CHOICE', d: 'Pilihan dialog raja: setia', v: true },
    { k: 'Q_SIDE_221_WOLF', d: 'Perburuan serigala putih selesai', v: false },
    { k: 'EVT_ECLIPSE', d: 'Peristiwa gerhana aktif', v: false },
  ])
  const presets = type === 'Item'
    ? [['0x00A1F3', 'Pedang Fajar'], ['0x00B220', 'Elixir Agung'], ['0x00C9E1', 'Zirah Naga']]
    : [['0x7F0011', 'Raja Abu'], ['0x7F0042', 'Wyvern Tua'], ['0x7F00A0', 'Pedagang Misterius']]
  const spawn = () => {
    const s = `${type} ${id} ×${qty}`
    setRecent((r) => [s, ...r].slice(0, 6))
    push(`Spawn ${s}`, true)
  }
  return (
    <>
      <Panel title="Item / NPC Spawner" code="SPAWN · ID" icon={Package}>
        <div className="flex flex-wrap gap-2">
          <div className="relative flex rounded-lg border border-line bg-ink/50 p-0.5">
            {(['Item', 'NPC'] as const).map((t) => (
              <button key={t} onClick={() => setType(t)} className="relative px-4 text-xs">
                {type === t && <motion.div layoutId="spt" className="absolute inset-0 rounded-md bg-bone" />}
                <span className={`relative ${type === t ? 'font-medium text-ink' : 'text-dim'}`}>{t}</span>
              </button>
            ))}
          </div>
          <input value={id} onChange={(e) => setId(e.target.value)} className="min-w-40 flex-1 rounded-lg border border-line bg-ink/50 px-3 py-2 font-mono text-sm outline-none focus:border-amber/60" />
          <input type="number" min={1} value={qty} onChange={(e) => setQty(Math.max(1, +e.target.value))} className="w-20 rounded-lg border border-line bg-ink/50 px-3 py-2 font-mono text-sm outline-none focus:border-amber/60" />
          <button onClick={spawn} className="flex items-center gap-1.5 rounded-lg bg-gradient-to-b from-[#ffc53d] to-amber px-5 text-sm font-semibold text-ink shadow-[0_6px_20px_-6px_rgba(255,176,0,0.7)]">
            <Sparkles size={14} /> Spawn
          </button>
        </div>
        <div className="mt-3 flex flex-wrap gap-2">
          {presets.map(([pid, n]) => (
            <button key={pid} onClick={() => setId(pid)} className={`rounded-md border px-2.5 py-1 text-[11px] ${id === pid ? 'border-amber/50 text-amber' : 'border-line text-dim hover:text-bone'}`}>
              {n} <span className="font-mono opacity-60">{pid}</span>
            </button>
          ))}
        </div>
        {recent.length > 0 && (
          <div className="mt-4 flex flex-wrap items-center gap-2 border-t border-line/70 pt-4 font-mono text-[10px] text-dim">
            <span>TERAKHIR</span>
            {recent.map((r, i) => <span key={i} className="rounded border border-line bg-ink/40 px-2 py-0.5">{r}</span>)}
          </div>
        )}
      </Panel>
      <Panel title="Quest / Event Flag Editor" code="FLAGS · RAW" icon={Flag}>
        <div className="overflow-hidden rounded-xl border border-line">
          <table className="w-full text-left text-[13px]">
            <thead className="bg-ink/50 font-mono text-[10px] tracking-widest text-dim">
              <tr><th className="px-4 py-2.5">FLAG</th><th className="px-4 py-2.5">DESKRIPSI</th><th className="px-4 py-2.5 text-right">NILAI</th></tr>
            </thead>
            <tbody>
              {flags.map((f, i) => (
                <tr key={f.k} className="border-t border-line/70 transition-colors hover:bg-amber/[0.04]">
                  <td className="px-4 py-3 font-mono text-[11px] text-amber">{f.k}</td>
                  <td className="px-4 py-3 text-dim">{f.d}</td>
                  <td className="px-4 py-3">
                    <div className="flex justify-end">
                      <Switch size="sm" on={f.v} onClick={() => { setFlags(flags.map((x, j) => (j === i ? { ...x, v: !x.v } : x))); push(`${f.k} = ${!f.v ? 1 : 0}`, !f.v) }} />
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Panel>
    </>
  )
}
