import React, { useState } from 'react'

export default function AlertBanner({ activeAlert, onClear, alarmActive }) {
  const [showLatent, setShowLatent] = useState(false)

  if (!alarmActive && !activeAlert) {
    return null
  }

  const p = activeAlert?.payload || {}
  const incident = p.incident_metrics || {}
  const model = p.edge_model || {}
  const latent = model.latent_vector || []

  return (
    <div
      className="glow-danger pulse"
      style={{
        backgroundColor: 'rgba(239, 68, 68, 0.1)',
        border: '1px solid var(--accent-danger)',
        borderRadius: '12px',
        padding: '1.25rem',
        marginBottom: '1.5rem'
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: '1rem' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
          <div style={{ width: '12px', height: '12px', borderRadius: '50%', backgroundColor: 'var(--accent-danger)' }}></div>
          <div>
            <h2 style={{ fontSize: '1.15rem', fontWeight: 800, color: '#f87171', letterSpacing: '-0.02em' }}>
              CRITICAL: ANOMALY DETECTED (ALARM LATCHED)
            </h2>
            <p style={{ fontSize: '0.8rem', color: '#fca5a5' }}>
              Fault condition sustained across {incident.anomaly_count || 10}/20 windows | Latch duration: {p.alarm_latch_ms || 3000}ms
            </p>
          </div>
        </div>

        <div style={{ display: 'flex', gap: '0.75rem', alignItems: 'center' }}>
          <button
            onClick={() => setShowLatent(!showLatent)}
            style={{
              padding: '0.35rem 0.75rem',
              fontSize: '0.75rem',
              fontWeight: 600,
              borderRadius: '6px',
              backgroundColor: 'var(--bg-secondary)',
              color: 'var(--text-main)',
              border: '1px solid var(--border-accent)',
              cursor: 'pointer'
            }}
          >
            {showLatent ? 'Hide Latent Space' : 'View 16D Latent Code'}
          </button>

          <button
            onClick={onClear}
            style={{
              padding: '0.35rem 0.85rem',
              fontSize: '0.75rem',
              fontWeight: 700,
              borderRadius: '6px',
              backgroundColor: 'var(--accent-danger)',
              color: '#fff',
              border: 'none',
              cursor: 'pointer'
            }}
          >
            Acknowledge / Reset
          </button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(140px, 1fr))', gap: '0.75rem', marginTop: '1rem' }}>
        <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '0.5rem 0.75rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.7rem', color: '#fca5a5' }}>PEAK MSE</div>
          <div style={{ fontSize: '1.1rem', fontWeight: 700, fontFamily: 'var(--font-mono)' }}>
            {(incident.peak_mse || 0).toFixed(6)}
          </div>
        </div>

        <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '0.5rem 0.75rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.7rem', color: '#fca5a5' }}>DETECTION DELAY</div>
          <div style={{ fontSize: '1.1rem', fontWeight: 700, fontFamily: 'var(--font-mono)' }}>
            {incident.detection_delay_ms || 1240} ms
          </div>
        </div>

        <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '0.5rem 0.75rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.7rem', color: '#fca5a5' }}>TRIP RATIO</div>
          <div style={{ fontSize: '1.1rem', fontWeight: 700, fontFamily: 'var(--font-mono)' }}>
            {incident.anomaly_count || 12} / {incident.window_span || 20}
          </div>
        </div>

        <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '0.5rem 0.75rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.7rem', color: '#fca5a5' }}>MODEL ARCH</div>
          <div style={{ fontSize: '1.1rem', fontWeight: 700, fontFamily: 'var(--font-mono)' }}>
            {model.architecture || 'cAE INT8'}
          </div>
        </div>
      </div>

      {showLatent && latent.length > 0 && (
        <div style={{ marginTop: '1rem', padding: '0.75rem', backgroundColor: 'var(--bg-secondary)', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '0.75rem', fontWeight: 600, color: 'var(--accent-cyan)', marginBottom: '0.5rem' }}>
            INT8 Bottleneck Bottleneck Vector (16-Dimensional Compressed State):
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(8, 1fr)', gap: '6px' }}>
            {latent.map((val, idx) => (
              <div
                key={idx}
                style={{
                  textAlign: 'center',
                  padding: '0.25rem',
                  fontSize: '0.75rem',
                  fontFamily: 'var(--font-mono)',
                  backgroundColor: val < 0 ? 'rgba(239,68,68,0.2)' : 'rgba(6,182,212,0.2)',
                  border: `1px solid ${val < 0 ? 'rgba(239,68,68,0.4)' : 'rgba(6,182,212,0.4)'}`,
                  borderRadius: '4px'
                }}
              >
                {val}
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
