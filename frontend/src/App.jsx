// ============================================================
// App.jsx — Componente raíz de la aplicación
// ============================================================
import { useState, useEffect } from 'react'
import Terminal    from './components/Terminal'
import FileExplorer from './components/FileExplorer'
import LoginModal  from './components/LoginModal'
import { apiLogout, getMounted, getSession } from './api/api'

export default function App() {
  const [activeTab,  setActiveTab]  = useState('terminal')
  const [session,    setSession]    = useState({ loggedIn: false, user: '', group: '', partitionId: '' })
  const [showLogin,  setShowLogin]  = useState(false)
  const [mounted,    setMounted]    = useState([])

  // ── Restaurar estado de sesión al cargar ──────────────────
  useEffect(() => {
    (async () => {
      try {
        const [s, m] = await Promise.all([getSession(), getMounted()])
        setSession(s)
        setMounted(m)
      } catch { /* backend no disponible aún */ }
    })()
  }, [])

  // ── Recargar particiones montadas ─────────────────────────
  const refreshMounted = async () => {
    try { setMounted(await getMounted()) } catch {}
  }

  // ── Logout ────────────────────────────────────────────────
  const handleLogout = async () => {
    await apiLogout()
    setSession({ loggedIn: false, user: '', group: '', partitionId: '' })
  }

  return (
    <div className="app-layout">
      {/* ── Navbar ──────────────────────────────────────── */}
      <nav className="navbar">
        <div className="navbar-brand">
          💽 <span>MIA</span> Disk Manager
          <span style={{fontSize:'12px', color:'var(--text-secondary)', marginLeft:'4px'}}>P2</span>
        </div>

        <div className="tabs">
          <button className={`tab ${activeTab==='terminal'  ?'active':''}`}
                  onClick={() => { setActiveTab('terminal');  refreshMounted() }}>
            ⌨ Terminal
          </button>
          <button className={`tab ${activeTab==='explorer'  ?'active':''}`}
                  onClick={() => { setActiveTab('explorer');  refreshMounted() }}>
            🗂 Explorador
          </button>
        </div>

        <div className="navbar-actions">
          {session.loggedIn ? (
            <>
              <div className="session-badge">
                <span className="dot"/>
                {session.user} @ {session.partitionId}
              </div>
              <button className="btn btn-danger btn-sm" onClick={handleLogout}>
                Cerrar Sesión
              </button>
            </>
          ) : (
            <button className="btn btn-success btn-sm"
                    onClick={() => { refreshMounted(); setShowLogin(true) }}>
              🔐 Iniciar Sesión
            </button>
          )}
        </div>
      </nav>

      {/* ── Contenido principal ──────────────────────────── */}
      <main className="overflow-hidden">
        {activeTab === 'terminal' && (
          <Terminal
            session={session}
            onLoginClick={() => { refreshMounted(); setShowLogin(true) }}
            onLogout={handleLogout}
          />
        )}
        {activeTab === 'explorer' && (
          <FileExplorer session={session} />
        )}
      </main>

      {/* ── Modal de login ───────────────────────────────── */}
      {showLogin && (
        <LoginModal
          mounted={mounted}
          onClose={() => setShowLogin(false)}
          onSuccess={(s) => { setSession(s); setShowLogin(false) }}
        />
      )}
    </div>
  )
}
