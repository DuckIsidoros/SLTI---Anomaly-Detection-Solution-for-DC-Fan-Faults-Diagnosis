import React, { useState } from 'react'

export default function FftChart({ fft }) {
  const [activeAxis, setActiveAxis] = useState('all')

  if (!fft || !fft.axes) {
    return (
      <div className="card h-full flex items-center justify-center text-gray-500 font-mono text-sm">
        Waiting for FFT Spectrum Data...
      </div>
    )
  }

  const { bin_count = 128, freq_start_hz = 10.74, freq_end_hz = 134.77, axes } = fft
  const xData = axes.x || []
  const yData = axes.y || []
  const zData = axes.z || []

  const maxVal = Math.max(
    0.1,
    ...xData.slice(0, bin_count),
    ...yData.slice(0, bin_count),
    ...zData.slice(0, bin_count)
  )

  // SVG coordinates
  const width = 640
  const height = 200
  const padding = { top: 20, right: 20, bottom: 30, left: 45 }
  const plotWidth = width - padding.left - padding.right
  const plotHeight = height - padding.top - padding.bottom

  const getPoints = (data) => {
    if (!data.length) return ''
    return data
      .map((val, idx) => {
        const x = padding.left + (idx / (bin_count - 1)) * plotWidth
        const y = padding.top + plotHeight - (val / maxVal) * plotHeight
        return `${x.toFixed(1)},${y.toFixed(1)}`
      })
      .join(' ')
  }

  return (
    <div className="card flex flex-col justify-between" style={{ minHeight: '320px' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem' }}>
        <div>
          <h3 style={{ fontSize: '1rem', fontWeight: 700, letterSpacing: '-0.02em', display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <span style={{ display: 'inline-block', width: '8px', height: '8px', borderRadius: '50%', backgroundColor: 'var(--accent-cyan)' }}></span>
            Vibration FFT Amplitude Spectrum (128 Bins)
          </h3>
          <p style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
            Bandpass: {freq_start_hz.toFixed(2)} Hz – {freq_end_hz.toFixed(2)} Hz | Fs = 500 Hz | Unit: {fft.unit || 'g'}
          </p>
        </div>

        <div style={{ display: 'flex', gap: '0.25rem', backgroundColor: 'var(--bg-secondary)', padding: '0.25rem', borderRadius: '6px', border: '1px solid var(--border-subtle)' }}>
          {['all', 'x', 'y', 'z'].map((axis) => (
            <button
              key={axis}
              onClick={() => setActiveAxis(axis)}
              style={{
                padding: '0.2rem 0.6rem',
                fontSize: '0.75rem',
                fontWeight: 600,
                textTransform: 'uppercase',
                borderRadius: '4px',
                border: 'none',
                cursor: 'pointer',
                backgroundColor: activeAxis === axis ? 'var(--border-accent)' : 'transparent',
                color: activeAxis === axis ? '#fff' : 'var(--text-muted)'
              }}
            >
              {axis}
            </button>
          ))}
        </div>
      </div>

      <div style={{ width: '100%', overflowX: 'auto' }}>
        <svg viewBox={`0 0 ${width} ${height}`} style={{ width: '100%', height: 'auto', display: 'block' }}>
          {/* Grid lines */}
          {[0, 0.25, 0.5, 0.75, 1].map((pct) => {
            const y = padding.top + plotHeight * (1 - pct)
            const val = (pct * maxVal).toFixed(2)
            return (
              <g key={pct}>
                <line x1={padding.left} y1={y} x2={width - padding.right} y2={y} stroke="var(--border-subtle)" strokeDasharray="3 3" />
                <text x={padding.left - 8} y={y + 4} fill="var(--text-dim)" fontSize="10" fontFamily="var(--font-mono)" textAnchor="end">
                  {val}
                </text>
              </g>
            )
          })}

          {/* Frequency labels along bottom */}
          {[0, 0.25, 0.5, 0.75, 1].map((pct) => {
            const x = padding.left + pct * plotWidth
            const f = (freq_start_hz + pct * (freq_end_hz - freq_start_hz)).toFixed(0)
            return (
              <text key={pct} x={x} y={height - 8} fill="var(--text-dim)" fontSize="10" fontFamily="var(--font-mono)" textAnchor="middle">
                {f}Hz
              </text>
            )
          })}

          {/* Spectrum lines */}
          {(activeAxis === 'all' || activeAxis === 'x') && (
            <polyline fill="none" stroke="#ef4444" strokeWidth="1.8" points={getPoints(xData)} opacity={activeAxis === 'all' ? 0.85 : 1} />
          )}
          {(activeAxis === 'all' || activeAxis === 'y') && (
            <polyline fill="none" stroke="#10b981" strokeWidth="1.8" points={getPoints(yData)} opacity={activeAxis === 'all' ? 0.85 : 1} />
          )}
          {(activeAxis === 'all' || activeAxis === 'z') && (
            <polyline fill="none" stroke="#06b6d4" strokeWidth="2.2" points={getPoints(zData)} opacity={activeAxis === 'all' ? 0.95 : 1} />
          )}
        </svg>
      </div>

      <div style={{ display: 'flex', gap: '1.5rem', marginTop: '0.75rem', fontSize: '0.75rem', fontFamily: 'var(--font-mono)' }}>
        <span style={{ display: 'flex', alignItems: 'center', gap: '0.35rem', color: '#ef4444' }}>
          <span style={{ width: '10px', height: '3px', backgroundColor: '#ef4444' }}></span> Axis X
        </span>
        <span style={{ display: 'flex', alignItems: 'center', gap: '0.35rem', color: '#10b981' }}>
          <span style={{ width: '10px', height: '3px', backgroundColor: '#10b981' }}></span> Axis Y
        </span>
        <span style={{ display: 'flex', alignItems: 'center', gap: '0.35rem', color: '#06b6d4' }}>
          <span style={{ width: '10px', height: '3px', backgroundColor: '#06b6d4' }}></span> Axis Z (Primary)
        </span>
      </div>
    </div>
  )
}
