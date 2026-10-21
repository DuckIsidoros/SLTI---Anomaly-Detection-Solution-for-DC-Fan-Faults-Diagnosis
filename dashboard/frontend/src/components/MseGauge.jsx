import React from 'react'

export default function MseGauge({ inference, anomalyRing }) {
  const currentMse = inference?.current_mse || 0.0
  const threshold = inference?.dynamic_threshold || 0.00785
  const isAnomaly = inference?.is_window_anomaly || false

  const anomalyCount = anomalyRing?.anomaly_count || 0
  const windowSpan = anomalyRing?.window_span || 20
  const triggerThreshold = anomalyRing?.trigger_threshold || 10

  const ratio = Math.min(2.5, currentMse / (threshold || 1e-5))
  const gaugePercent = Math.min(100, Math.max(0, (ratio / 2.0) * 100))

  return (
    <div className={`card ${isAnomaly ? 'glow-danger' : ''}`} style={{ minHeight: '320px', display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
      <div>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <h3 style={{ fontSize: '1rem', fontWeight: 700, display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <span style={{ display: 'inline-block', width: '8px', height: '8px', borderRadius: '50%', backgroundColor: isAnomaly ? 'var(--accent-danger)' : 'var(--accent-green)' }}></span>
            Reconstruction MSE & Anomaly Gauge
          </h3>
          <span className={isAnomaly ? 'pill pill-danger' : 'pill pill-green'}>
            {isAnomaly ? 'WINDOW ANOMALY' : 'NORMAL'}
          </span>
        </div>
        <p style={{ fontSize: '0.75rem', color: 'var(--text-dim)', marginTop: '0.25rem' }}>
          cAE INT8 Reconstruction Loss against 99th-percentile dynamic baseline
        </p>
      </div>

      {/* Main Metrics Display */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '1rem', margin: '1rem 0' }}>
        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.85rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', textTransform: 'uppercase', fontFamily: 'var(--font-mono)' }}>Instantaneous MSE</div>
          <div style={{ fontSize: '1.6rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: isAnomaly ? 'var(--accent-danger)' : 'var(--text-main)' }}>
            {currentMse.toFixed(6)}
          </div>
          <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>
            Ratio: {(currentMse / (threshold || 1e-5)).toFixed(2)}x baseline
          </div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.85rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
          <div style={{ fontSize: '0.75rem', color: 'var(--text-dim)', textTransform: 'uppercase', fontFamily: 'var(--font-mono)' }}>Dynamic Threshold</div>
          <div style={{ fontSize: '1.6rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--accent-cyan)' }}>
            {threshold.toFixed(6)}
          </div>
          <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>
            Empirical 99th %ile
          </div>
        </div>
      </div>

      {/* Gauge bar */}
      <div style={{ marginBottom: '1rem' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.75rem', fontFamily: 'var(--font-mono)', color: 'var(--text-dim)', marginBottom: '0.25rem' }}>
          <span>0.0</span>
          <span style={{ color: 'var(--accent-cyan)' }}>Threshold ({threshold.toFixed(5)})</span>
          <span>2.0x Limit</span>
        </div>
        <div style={{ width: '100%', height: '8px', backgroundColor: 'var(--bg-secondary)', borderRadius: '4px', overflow: 'hidden', position: 'relative' }}>
          <div
            style={{
              height: '100%',
              width: `${gaugePercent}%`,
              backgroundColor: isAnomaly ? 'var(--accent-danger)' : 'var(--accent-green)',
              transition: 'width 0.3s ease, background-color 0.3s ease'
            }}
          />
          {/* Threshold marker tick at 50% */}
          <div style={{ position: 'absolute', top: 0, bottom: 0, left: '50%', width: '2px', backgroundColor: 'var(--accent-cyan)' }} />
        </div>
      </div>

      {/* 20-Window Rolling Ring Gauge */}
      <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.75rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.5rem' }}>
          <span style={{ fontSize: '0.75rem', fontWeight: 600 }}>Rolling Anomaly Ring Buffer</span>
          <span style={{ fontSize: '0.75rem', fontFamily: 'var(--font-mono)', color: anomalyCount >= triggerThreshold ? 'var(--accent-danger)' : 'var(--text-muted)' }}>
            <strong>{anomalyCount}</strong> / {windowSpan} (Trigger $\ge$ {triggerThreshold})
          </span>
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(20, 1fr)', gap: '4px' }}>
          {Array.from({ length: windowSpan }).map((_, i) => {
            const filled = i < anomalyCount
            return (
              <div
                key={i}
                style={{
                  height: '14px',
                  borderRadius: '2px',
                  backgroundColor: filled ? (anomalyCount >= triggerThreshold ? 'var(--accent-danger)' : 'var(--accent-warning)') : 'var(--border-subtle)',
                  transition: 'background-color 0.2s'
                }}
                title={`Window ${i + 1}`}
              />
            )
          })}
        </div>
      </div>
    </div>
  )
}
