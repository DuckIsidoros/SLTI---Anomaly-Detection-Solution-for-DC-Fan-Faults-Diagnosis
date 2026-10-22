import React, { useState, useEffect, useRef } from 'react'
import FftChart from './components/FftChart'
import MseGauge from './components/MseGauge'
import AlertBanner from './components/AlertBanner'
import LatencyCard from './components/LatencyCard'
import HealthCard from './components/HealthCard'
import AlertLog from './components/AlertLog'

export default function App() {
  const [wsConnected, setWsConnected] = useState(false)
  const [deviceStatus, setDeviceStatus] = useState({
    status: 'OFFLINE',
    device_id: 'esp32_edge_01',
    mqtt_connected: false
  })
  const [heartbeat, setHeartbeat] = useState(null)
  const [latestTelemetry, setLatestTelemetry] = useState(null)
  const [alerts, setAlerts] = useState([])
  const [alarmActive, setAlarmActive] = useState(false)
  const wsRef = useRef(null)

  useEffect(() => {
    let reconnectTimeout = null

    const connectWs = () => {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      const host = window.location.host
      // During Vite dev with proxy, use current host, or localhost:8000
      const wsUrl = `${protocol}//${host}/ws`

      const ws = new WebSocket(wsUrl)
      wsRef.current = ws

      ws.onopen = () => {
        setWsConnected(true)
      }

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data)
          if (msg.type === 'initial_state') {
            const d = msg.data
            if (d.device_status) setDeviceStatus(d.device_status)
            if (d.heartbeat !== undefined) setHeartbeat(d.heartbeat)
            if (d.latest_telemetry !== undefined) setLatestTelemetry(d.latest_telemetry)
            if (d.alerts) setAlerts(d.alerts)
            if (d.alarm_active !== undefined) setAlarmActive(d.alarm_active)
          } else if (msg.type === 'broker_status') {
            setDeviceStatus((prev) => ({ ...prev, mqtt_connected: msg.data.connected }))
          } else if (msg.type === 'telemetry') {
            setLatestTelemetry(msg.data)
            setDeviceStatus((prev) => ({
              ...prev,
              status: 'ONLINE',
              device_id: msg.data.device_id || prev.device_id
            }))
          } else if (msg.type === 'alert') {
            setAlarmActive(true)
            setAlerts((prev) => [msg.data, ...prev.slice(0, 49)])
          } else if (msg.type === 'heartbeat') {
            setHeartbeat(msg.data.payload)
            setDeviceStatus((prev) => ({
              ...prev,
              status: 'ONLINE',
              device_id: msg.data.device_id || prev.device_id
            }))
          } else if (msg.type === 'status') {
            setDeviceStatus((prev) => ({
              ...prev,
              status: msg.data.status,
              device_id: msg.data.device_id || prev.device_id
            }))
          } else if (msg.type === 'alarm_cleared') {
            setAlarmActive(false)
          }
        } catch (err) {
          console.error('Failed to parse WebSocket event:', err)
        }
      }

      ws.onclose = () => {
        setWsConnected(false)
        reconnectTimeout = setTimeout(connectWs, 2000)
      }

      ws.onerror = () => {
        ws.close()
      }
    }

    connectWs()

    return () => {
      if (reconnectTimeout) clearTimeout(reconnectTimeout)
      if (wsRef.current) wsRef.current.close()
    }
  }, [])

  const handleClearAlert = async () => {
    try {
      await fetch('/api/alerts/clear', { method: 'POST' })
      setAlarmActive(false)
    } catch (e) {
      console.error('Error clearing alert:', e)
    }
  }

  const isEdgeOnline = deviceStatus.status === 'ONLINE'
  const p = latestTelemetry?.payload || {}
  const inference = p.inference || null
  const anomalyRing = p.anomaly_ring || null
  const latency = p.latency_us || null
  const fft = p.fft || null

  return (
    <div style={{ maxWidth: '1440px', margin: '0 auto', padding: '1.5rem' }}>
      {/* Header Bar */}
      <header
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          flexWrap: 'wrap',
          gap: '1rem',
          paddingBottom: '1.25rem',
          marginBottom: '1.5rem',
          borderBottom: '1px solid var(--border-subtle)'
        }}
      >
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
            <h1 style={{ fontSize: '1.35rem', fontWeight: 800, letterSpacing: '-0.03em', background: 'linear-gradient(90deg, #f8fafc 0%, #94a3b8 100%)', WebkitBackgroundClip: 'text', WebkitTextFillColor: 'transparent' }}>
              Edge Condition Monitoring & TinyML Anomaly Dashboard
            </h1>
            <span className="pill pill-cyan">ISO 13374 / IEC 62443</span>
          </div>
          <p style={{ fontSize: '0.8rem', color: 'var(--text-muted)', fontFamily: 'var(--font-mono)', marginTop: '0.2rem' }}>
            Topic Branch: <span style={{ color: 'var(--accent-cyan)' }}>v1/plant_hcm/line_01/cnc_05/accelerometer</span>
          </p>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '0.75rem' }}>
          <span className={wsConnected ? 'pill pill-green' : 'pill pill-danger'}>
            <span style={{ width: '6px', height: '6px', borderRadius: '50%', backgroundColor: wsConnected ? '#34d399' : '#f87171' }}></span>
            {wsConnected ? 'LIVE WS' : 'WS RECONNECTING'}
          </span>

          <span className={isEdgeOnline ? 'pill pill-green' : 'pill pill-gray'}>
            <span style={{ width: '6px', height: '6px', borderRadius: '50%', backgroundColor: isEdgeOnline ? '#34d399' : '#94a3b8' }}></span>
            {isEdgeOnline ? 'NODE ONLINE' : 'NODE OFFLINE'}
          </span>

          <div style={{ fontSize: '0.8rem', fontFamily: 'var(--font-mono)', color: 'var(--text-dim)', backgroundColor: 'var(--bg-secondary)', padding: '0.3rem 0.65rem', borderRadius: '6px', border: '1px solid var(--border-subtle)' }}>
            Asset: <strong>cnc_05</strong>
          </div>
        </div>
      </header>

      {/* Edge Connection Gate: Only display active dashboard if edge connects successfully */}
      {!isEdgeOnline ? (
        <div
          className="card"
          style={{
            padding: '3rem 2rem',
            textAlign: 'center',
            marginBottom: '1.5rem',
            border: '1px dashed var(--border-accent)',
            backgroundColor: 'rgba(15, 23, 42, 0.6)'
          }}
        >
          <div
            style={{
              width: '72px',
              height: '72px',
              borderRadius: '50%',
              backgroundColor: 'rgba(56, 189, 248, 0.1)',
              border: '2px solid var(--accent-cyan)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: '1.8rem',
              margin: '0 auto 1.25rem',
              color: 'var(--accent-cyan)'
            }}
          >
            📡
          </div>

          <h2 style={{ fontSize: '1.4rem', fontWeight: 800, marginBottom: '0.5rem', letterSpacing: '-0.02em' }}>
            Awaiting Edge Node Connection
          </h2>
          <p style={{ maxWidth: '640px', margin: '0 auto 2rem', fontSize: '0.9rem', color: 'var(--text-dim)', lineHeight: 1.6 }}>
            The live telemetry processing pipeline and condition monitoring widgets are paused. The web dashboard will automatically activate once the edge device connects and starts streaming inference data.
          </p>

          <div
            style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fit, minmax(220px, 1fr))',
              gap: '1rem',
              maxWidth: '860px',
              margin: '0 auto 2rem',
              textAlign: 'left'
            }}
          >
            <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '1rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)', textTransform: 'uppercase', fontFamily: 'var(--font-mono)' }}>MQTT Broker</div>
              <div style={{ fontSize: '0.95rem', fontWeight: 700, marginTop: '0.25rem', color: deviceStatus.mqtt_connected ? 'var(--accent-green)' : 'var(--accent-warning)' }}>
                {deviceStatus.mqtt_connected ? 'Broker Online (8883)' : 'Awaiting Broker...'}
              </div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', marginTop: '0.25rem' }}>Mosquitto TLS 1.3</div>
            </div>

            <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '1rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)', textTransform: 'uppercase', fontFamily: 'var(--font-mono)' }}>WebSocket Link</div>
              <div style={{ fontSize: '0.95rem', fontWeight: 700, marginTop: '0.25rem', color: wsConnected ? 'var(--accent-green)' : 'var(--accent-danger)' }}>
                {wsConnected ? 'Backend Stream Ready' : 'Reconnecting...'}
              </div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', marginTop: '0.25rem' }}>ws://localhost:8000/ws</div>
            </div>

            <div style={{ backgroundColor: 'var(--bg-secondary)', padding: '1rem', borderRadius: '8px', border: '1px solid var(--border-subtle)' }}>
              <div style={{ fontSize: '0.7rem', color: 'var(--text-dim)', textTransform: 'uppercase', fontFamily: 'var(--font-mono)' }}>Target Edge Asset</div>
              <div style={{ fontSize: '0.95rem', fontWeight: 700, marginTop: '0.25rem', color: 'var(--text-main)', fontFamily: 'var(--font-mono)' }}>
                {deviceStatus.device_id || 'esp32_edge_01'}
              </div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)', marginTop: '0.25rem' }}>Asset: cnc_05 / accel</div>
            </div>
          </div>

          <div
            style={{
              display: 'inline-flex',
              flexDirection: 'column',
              alignItems: 'center',
              backgroundColor: 'var(--bg-primary)',
              border: '1px solid var(--border-subtle)',
              borderRadius: '8px',
              padding: '0.85rem 1.5rem',
              fontSize: '0.8rem',
              fontFamily: 'var(--font-mono)',
              color: 'var(--text-muted)'
            }}
          >
            <span style={{ color: 'var(--text-dim)', marginBottom: '0.35rem' }}>To launch an edge node or start telemetry publishing:</span>
            <span style={{ color: 'var(--accent-cyan)', fontWeight: 600 }}>python dashboard/simulate_edge_node.py</span>
          </div>
        </div>
      ) : (
        <>
          {/* Latched Alarm Alert Banner */}
          <AlertBanner
            activeAlert={alerts[0]}
            alarmActive={alarmActive}
            onClear={handleClearAlert}
          />

          {/* Main Grid: 2 Columns */}
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(460px, 1fr))', gap: '1.25rem', marginBottom: '1.25rem' }}>
            <MseGauge inference={inference} anomalyRing={anomalyRing} />
            <FftChart fft={fft} />
          </div>

          {/* Secondary Row: Latency & Health */}
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(460px, 1fr))', gap: '1.25rem', marginBottom: '1.25rem' }}>
            <LatencyCard latency={latency} />
            <HealthCard heartbeat={heartbeat} />
          </div>
        </>
      )}

      {/* Historical Incident Audit Log */}
      <AlertLog alerts={alerts} />

      {/* Footer */}
      <footer style={{ marginTop: '2rem', textAlign: 'center', fontSize: '0.75rem', color: 'var(--text-dim)', fontFamily: 'var(--font-mono)' }}>
        TinyML Vibration Anomaly Detection (cAE INT8) &bull; ESP32-WROOM-32 &bull; MPU-6050 &bull; Eclipse Mosquitto MQTTS 8883
      </footer>
    </div>
  )
}
