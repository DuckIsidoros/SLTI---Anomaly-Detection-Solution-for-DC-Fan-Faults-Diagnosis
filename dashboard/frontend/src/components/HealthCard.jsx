import React from 'react'

export default function HealthCard({ heartbeat }) {
  const hb = heartbeat || {}
  const hw = hb.hardware_health || {}
  const sys = hb.system_resources || {}
  const freeKb = sys.free_heap_bytes !== undefined ? Math.round(sys.free_heap_bytes / 1024) : '--'
  const minKb = sys.min_free_heap_bytes !== undefined ? Math.round(sys.min_free_heap_bytes / 1024) : '--'

  const formatUptime = (sec) => {
    if (!sec) return '0s'
    const h = Math.floor(sec / 3600)
    const m = Math.floor((sec % 3600) / 60)
    const s = sec % 60
    return `${h}h ${m}m ${s}s`
  }

  return (
    <div className="card" style={{ display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
      <div>
        <h3 style={{ fontSize: '0.95rem', fontWeight: 700, display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <span style={{ display: 'inline-block', width: '8px', height: '8px', borderRadius: '50%', backgroundColor: 'var(--accent-green)' }}></span>
          Hardware & Fleet Telemetry
        </h3>
        <p style={{ fontSize: '0.75rem', color: 'var(--text-dim)', marginTop: '0.2rem' }}>
          Heartbeat diagnostics, MPU6050 bus health & FreeRTOS memory
        </p>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '0.5rem', margin: '0.85rem 0' }}>
        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>Internal SRAM Free</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--accent-green)' }}>
            {freeKb} {freeKb !== '--' && <span style={{ fontSize: '0.7rem' }}>KB</span>}
          </div>
          <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)' }}>Low watermark: {minKb} {minKb !== '--' && 'KB'}</div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>Device Uptime</div>
          <div style={{ fontSize: '1.2rem', fontWeight: 800, fontFamily: 'var(--font-mono)', color: 'var(--text-main)' }}>
            {formatUptime(hb.uptime_seconds)}
          </div>
          <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)' }}>Target: Continuous</div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>MPU-6050 I2C Bus</div>
          <div style={{ fontSize: '1.05rem', fontWeight: 700, fontFamily: 'var(--font-mono)', color: (hw.i2c_consecutive_failures || 0) === 0 ? 'var(--accent-green)' : 'var(--accent-danger)' }}>
            {hw.i2c_consecutive_failures !== undefined ? ((hw.i2c_consecutive_failures === 0) ? 'HEALTHY (0 Drop)' : `${hw.i2c_consecutive_failures} Failures`) : '--'}
          </div>
          <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)' }}>ODR: {hw.mpu6050_odr_hz || '--'}Hz | &plusmn;{hw.mpu6050_range_g || '--'}g</div>
        </div>

        <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '0.65rem', borderRadius: '6px' }}>
          <div style={{ fontSize: '0.68rem', color: 'var(--text-dim)', textTransform: 'uppercase' }}>FreeRTOS Tasks</div>
          <div style={{ fontSize: '0.85rem', fontWeight: 700, fontFamily: 'var(--font-mono)', color: 'var(--accent-cyan)' }}>
            C0: {sys.core0_task || '--'}
          </div>
          <div style={{ fontSize: '0.85rem', fontWeight: 700, fontFamily: 'var(--font-mono)', color: 'var(--accent-blue)' }}>
            C1: {sys.core1_task || '--'}
          </div>
        </div>
      </div>

      <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)', display: 'flex', justifyContent: 'space-between' }}>
        <span>Environment: <strong>{hb.environment || 'ENV_TABLE'}</strong></span>
        <span>Calibration: {hb.calibration?.is_calibrated ? 'Complete (100w)' : 'In Progress'}</span>
      </div>
    </div>
  )
}
