// ============================================================
// Terminal.jsx — Componente de terminal de comandos
// ============================================================
import { useState, useRef, useEffect } from 'react'
import { executeCommand, executeScript } from '../api/api'

export default function Terminal({ session, onLoginClick, onLogout }) {
  const [history, setHistory]       = useState([
    { type: 'info', text: '╔══════════════════════════════════════╗' },
    { type: 'info', text: '║     MIA Disk Manager — Proyecto 2    ║' },
    { type: 'info', text: '║     Carnet: 68   EXT2/EXT3 FS        ║' },
    { type: 'info', text: '╚══════════════════════════════════════╝' },
    { type: 'output', text: 'Escribe un comando o carga un script .smia' },
  ])
  const [input, setInput]           = useState('')
  const [cmdHistory, setCmdHistory] = useState([])
  const [histIdx, setHistIdx]       = useState(-1)
  const [loading, setLoading]       = useState(false)
  const outputRef = useRef(null)
  const inputRef  = useRef(null)

  // ── Auto-scroll al fondo ─────────────────────────────────
  useEffect(() => {
    if (outputRef.current)
      outputRef.current.scrollTop = outputRef.current.scrollHeight
  }, [history])

  // ── Clasificar tipo de línea para color ──────────────────
  const classifyLine = (line) => {
    if (!line) return 'output'
    if (line.startsWith('SUCCESS:')) return 'success'
    if (line.startsWith('ERROR:'))   return 'error'
    if (line.startsWith('WARNING:')) return 'info'
    if (line.startsWith('INFO:'))    return 'info'
    if (line.startsWith('#'))        return 'comment'
    return 'output'
  }

  // ── Agregar líneas al historial ──────────────────────────
  const addOutput = (text) => {
    const lines = text.split('\n').filter(l => l !== undefined)
    setHistory(h => [...h, ...lines.map(l => ({
      type: classifyLine(l), text: l
    }))])
  }

  // ── Ejecutar comando ─────────────────────────────────────
  const runCommand = async (cmd) => {
    if (!cmd.trim()) return
    setHistory(h => [...h, { type: 'command', text: '$ ' + cmd }])
    setCmdHistory(h => [cmd, ...h.slice(0, 49)])
    setHistIdx(-1)
    setLoading(true)
    try {
      // LOGIN: abrir modal
      if (cmd.trim().toLowerCase() === 'login') {
        onLoginClick()
        setLoading(false)
        return
      }
      const data = await executeCommand(cmd)
      addOutput(data.output || '')
    } catch {
      addOutput('ERROR: No se pudo conectar al servidor backend\n')
    }
    setLoading(false)
  }

  // ── Manejar tecla Enter / flechas ────────────────────────
  const handleKey = (e) => {
    if (e.key === 'Enter') {
      runCommand(input)
      setInput('')
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      const idx = Math.min(histIdx + 1, cmdHistory.length - 1)
      setHistIdx(idx)
      setInput(cmdHistory[idx] || '')
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      const idx = Math.max(histIdx - 1, -1)
      setHistIdx(idx)
      setInput(idx === -1 ? '' : cmdHistory[idx])
    }
  }

  // ── Cargar script .smia ──────────────────────────────────
  const handleFile = (e) => {
    const file = e.target.files[0]
    if (!file) return
    const reader = new FileReader()
    reader.onload = async (ev) => {
      const script = ev.target.result
      setHistory(h => [...h, {
        type: 'info', text: `▶ Cargando script: ${file.name}`
      }])
      setLoading(true)
      try {
        const data = await executeScript(script)
        addOutput(data.output || '')
      } catch {
        addOutput('ERROR: No se pudo ejecutar el script\n')
      }
      setLoading(false)
    }
    reader.readAsText(file)
    e.target.value = ''
  }

  // ── Limpiar terminal ─────────────────────────────────────
  const clearTerminal = () => setHistory([])

  return (
    <div className="terminal-container full-height">
      {/* Salida */}
      <div className="terminal-output" ref={outputRef}
           onClick={() => inputRef.current?.focus()}>
        {history.map((line, i) => (
          <div key={i} className={`term-line ${line.type}`}>
            {line.text}
          </div>
        ))}
        {loading && <div className="term-line info">Procesando...</div>}
      </div>

      {/* Barra de entrada */}
      <div className="terminal-input-row">
        <span className="term-prompt">
          {session.loggedIn
            ? `[${session.user}@${session.partitionId}]$`
            : '[guest]$'}
        </span>
        <input
          ref={inputRef}
          className="term-input"
          value={input}
          onChange={e => setInput(e.target.value)}
          onKeyDown={handleKey}
          placeholder="Escribe un comando..."
          autoFocus
        />
        {/* Botones */}
        <label className="script-upload-label" title="Cargar script .smia">
          📄 Script
          <input type="file" accept=".smia,.txt" onChange={handleFile} hidden />
        </label>
        <button className="btn btn-secondary btn-sm" onClick={clearTerminal}>
          Limpiar
        </button>
        {session.loggedIn ? (
          <button className="btn btn-danger btn-sm" onClick={onLogout}>
            Logout
          </button>
        ) : (
          <button className="btn btn-success btn-sm" onClick={onLoginClick}>
            Login
          </button>
        )}
      </div>
    </div>
  )
}
