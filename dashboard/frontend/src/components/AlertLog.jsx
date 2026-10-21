import React from 'react'

export default function AlertLog({ alerts }) {
  if (!alerts || alerts.length === 0) {
    return (
      <div className="card" style={{ minHeight: '180px', display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', color: 'var(--text-dim)', fontSize: '0.85rem' }}>
        No historical alerts recorded. System operational within dynamic threshold.
      </div>
    )
  }

  return (
    <div className="card">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '0.75rem' }}>
        <h3 style={{ fontSize: '0.95rem', fontWeight: 700, display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <span style={{ display: 'inline-block', width: '8px', height: '8px', borderRadius: '50%', backgroundColor: 'var(--accent-danger)' }}></span>
          Incident Alert Audit Log (IEC 62443 Scoped)
        </h3>
        <span style={{ fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
          {alerts.length} Incidents Logged
        </span>
      </div>

      <div style={{ overflowX: 'auto' }}>
        <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.75rem', fontFamily: 'var(--font-mono)' }}>
          <thead>
            <tr style={{ borderBottom: '1px solid var(--border-subtle)', color: 'var(--text-dim)', textAlign: 'left' }}>
              <th style={{ padding: '0.5rem 0.75rem' }}>TIMESTAMP (UTC)</th>
              <th style={{ padding: '0.5rem 0.75rem' }}>EVENT</th>
              <th style={{ padding: '0.5rem 0.75rem' }}>PEAK MSE</th>
              <th style={{ padding: '0.5rem 0.75rem' }}>THRESHOLD</th>
              <th style={{ padding: '0.5rem 0.75rem' }}>VOTING RATIO</th>
              <th style={{ padding: '0.5rem 0.75rem' }}>DETECTION DELAY</th>
            </tr>
          </thead>
          <tbody>
            {alerts.slice(0, 8).map((al, idx) => {
              const p = al.payload || {}
              const m = p.incident_metrics || {}
              return (
                <tr key={idx} style={{ borderBottom: '1px solid var(--border-subtle)', backgroundColor: idx % 2 === 0 ? 'rgba(255,255,255,0.01)' : 'transparent' }}>
                  <td style={{ padding: '0.5rem 0.75rem', color: 'var(--text-muted)' }}>{al.timestamp || 'N/A'}</td>
                  <td style={{ padding: '0.5rem 0.75rem' }}>
                    <span className="pill pill-danger" style={{ fontSize: '0.65rem' }}>{p.event || 'ALARM_LATCHED'}</span>
                  </td>
                  <td style={{ padding: '0.5rem 0.75rem', color: '#f87171', fontWeight: 700 }}>
                    {(m.peak_mse || m.current_mse || 0).toFixed(6)}
                  </td>
                  <td style={{ padding: '0.5rem 0.75rem', color: 'var(--accent-cyan)' }}>
                    {(m.dynamic_threshold || 0).toFixed(6)}
                  </td>
                  <td style={{ padding: '0.5rem 0.75rem' }}>
                    {m.anomaly_count || 12} / {m.window_span || 20}
                  </td>
                  <td style={{ padding: '0.5rem 0.75rem', color: 'var(--text-muted)' }}>
                    {m.detection_delay_ms || 1240} ms
                  </td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </div>
    </div>
  )
}
