// ============================================================
// LoginModal.jsx — Modal de inicio de sesión
// ============================================================
import { useState } from 'react'
import { apiLogin } from '../api/api'

export default function LoginModal({ onClose, onSuccess, mounted }) {
  const [user,    setUser]    = useState('')
  const [pass,    setPass]    = useState('')
  const [partId,  setPartId]  = useState(mounted[0]?.id || '')
  const [error,   setError]   = useState('')
  const [loading, setLoading] = useState(false)

  // ── Enviar login ─────────────────────────────────────────
  const handleSubmit = async (e) => {
    e.preventDefault()
    if (!user || !pass || !partId) {
      setError('Completa todos los campos')
      return
    }
    setLoading(true)
    setError('')
    try {
      const data = await apiLogin(user, pass, partId)
      if (data.success) {
        onSuccess({ loggedIn: true, user: data.user, group: data.group, partitionId: partId })
        onClose()
      } else {
        setError(data.message || 'Credenciales incorrectas')
      }
    } catch {
      setError('Error de conexión al servidor')
    }
    setLoading(false)
  }

  return (
    <div className="modal-overlay" onClick={e => e.target === e.currentTarget && onClose()}>
      <div className="modal">
        {/* Título */}
        <div className="modal-title">
          🔐 Iniciar Sesión
        </div>

        <form onSubmit={handleSubmit}>
          {/* Usuario */}
          <div className="form-group">
            <label className="form-label">Usuario</label>
            <input
              className="form-input"
              type="text"
              value={user}
              onChange={e => setUser(e.target.value)}
              placeholder="ej: root"
              autoFocus
            />
          </div>

          {/* Contraseña */}
          <div className="form-group">
            <label className="form-label">Contraseña</label>
            <input
              className="form-input"
              type="password"
              value={pass}
              onChange={e => setPass(e.target.value)}
              placeholder="••••••"
            />
          </div>

          {/* Partición */}
          <div className="form-group">
            <label className="form-label">Partición</label>
            {mounted.length > 0 ? (
              <select
                className="form-input"
                value={partId}
                onChange={e => setPartId(e.target.value)}
              >
                {mounted.map(mp => (
                  <option key={mp.id} value={mp.id}>
                    {mp.id} — {mp.name} ({mp.fs === 3 ? 'EXT3' : 'EXT2'})
                  </option>
                ))}
              </select>
            ) : (
              <input
                className="form-input"
                type="text"
                value={partId}
                onChange={e => setPartId(e.target.value)}
                placeholder="ej: 682A"
              />
            )}
          </div>

          {/* Error */}
          {error && <p className="error-msg">{error}</p>}

          {/* Botones */}
          <div className="flex gap-2 mt-4">
            <button type="button" className="btn btn-secondary"
                    style={{flex:1}} onClick={onClose}>
              Cancelar
            </button>
            <button type="submit" className="btn btn-primary"
                    style={{flex:1}} disabled={loading}>
              {loading ? 'Verificando...' : 'Entrar'}
            </button>
          </div>
        </form>
      </div>
    </div>
  )
}
