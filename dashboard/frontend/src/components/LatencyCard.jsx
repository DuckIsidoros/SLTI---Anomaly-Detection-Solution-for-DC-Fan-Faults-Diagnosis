import React from 'react'

export default function LatencyCard({ latency }) {
  const dsp = latency?.dsp || 1240
  const inference = latency?.inference || 4850
  const e2e = latency?.e2e || 6820

  const dspPct = Math.round((dsp / e2e) * 100) || 18
  const infPct = Math.round((inference / e2e) * 100) || 71
  const overheadPct = Math.max(0, 100 - dspPct - infPct)

  return (
    <div className="card" style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
      <div>
        <h3 style={{ fontSize: '0.95rem', fontWeight: 700, display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <span style={{ display: 'inline-block', width: '8px', height: '8px', borderRadius: '50%', backgroundColor: 'var(--accent-blue)' }}></span>
          Pipeline Execution Latency
        </h3>
        <p style={{ fontSize: '0.75rem', color: 'var(--text-dim)', marginTop: '0.2rem' }}>
          Dual-Core ESP32 execution turnaround in microseconds ($\mu$s)
        </p>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '0.5rem', margin: '0.85rem 0' }}>
        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>DSP & Hann</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--accent-blue)' }}>
            {dsp} <span style={{ fontSize: '0.7rem' }}>$\mu$s</span>
          </div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>TFLM INT8</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--accent-cyan)' }}>
            {inference} <span style={{ fontSize: '0.7rem' }}>$\mu$s</span>
          </div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>End-to-End</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--accent-green)' }}>
            {e2e} <span style={{ fontSize: '0.7rem' }}>$\mu$s</span>
          </div>
        </div>
      </div>

      <div>
        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.7rem', color: 'var(--text-dim)', marginBottom: '0.25rem' }}>
          <span>Budget: {e2e} $\mu$s / 512,000 $\mu$s (Stride period)</span>
          <span>Load: {(e2e / 5120).toFixed(2)}%</span>
        </div>
        <div style={{ display: 'flex', height: '6px', borderRadius: '3px', overflow: 'hidden', backgroundColor: 'var(--bg-secondary)' }}>
          <div style={{ width: `${dspPct}%`, backgroundColor: 'var(--accent-blue)' }} title={`DSP: ${dspPct}%`} />
          <div style={{ width: `${infPct}%`, backgroundColor: 'var(--accent-cyan)' }} title={`Inference: ${infPct}%`} />
          <div style={{ width: `${overheadPct}%`, backgroundColor: 'var(--text-dim)' }} title={`Overhead: ${overheadPct}%`} />
        </div>
      </div>
    </div>
  )
}
