// ============================================================
// FileExplorer.jsx — Visualizador de sistema de archivos
// ============================================================
import { useState, useEffect } from 'react'
import { getMounted, getDisks, getFileTree, getFileContent, getJournal } from '../api/api'

// ── Nodo recursivo del árbol ─────────────────────────────────
function TreeNode({ node, depth = 0, onSelect, selected }) {
  const [open, setOpen] = useState(depth < 2)
  const isFolder = node.type === 'folder'
  const isSelected = selected === node

  const toggle = () => {
    if (isFolder) setOpen(!open)
    onSelect(node)
  }

  return (
    <div>
      <div
        className={`tree-node ${isSelected ? 'selected' : ''}`}
        style={{ paddingLeft: `${depth * 16 + 6}px` }}
        onClick={toggle}
      >
        <span className="tree-icon">
          {isFolder ? (open ? '📂' : '📁') : '📄'}
        </span>
        <span className="tree-name">{node.name}</span>
        <span className="tree-perm">{node.perm}</span>
      </div>
      {isFolder && open && node.children?.map((child, i) => (
        <TreeNode key={i} node={child} depth={depth + 1}
                  onSelect={onSelect} selected={selected} />
      ))}
    </div>
  )
}

// ── Componente principal del explorador ──────────────────────
export default function FileExplorer({ session }) {
  const [mounted,   setMounted]   = useState([])
  const [disks,     setDisks]     = useState([])
  const [selPart,   setSelPart]   = useState(null)
  const [fileTree,  setFileTree]  = useState(null)
  const [selNode,   setSelNode]   = useState(null)
  const [fileContent, setFileContent] = useState(null)
  const [journal,   setJournal]   = useState(null)
  const [tab,       setTab]       = useState('explorer')
  const [loading,   setLoading]   = useState(false)

  // ── Cargar particiones montadas ──────────────────────────
  const loadMounted = async () => {
    try {
      const [mnt, dsk] = await Promise.all([getMounted(), getDisks()])
      setMounted(mnt)
      setDisks(dsk)
    } catch { /* backend no disponible */ }
  }

  useEffect(() => { loadMounted() }, [])

  // ── Seleccionar partición ─────────────────────────────────
  const selectPartition = async (mp) => {
    setSelPart(mp)
    setSelNode(null)
    setFileContent(null)
    setJournal(null)
    setLoading(true)
    try {
      const tree = await getFileTree(mp.id)
      setFileTree(tree)
      if (mp.fs === 3) {
        const j = await getJournal(mp.id)
        setJournal(j)
      }
    } catch { setFileTree(null) }
    setLoading(false)
  }

  // ── Seleccionar nodo en árbol ────────────────────────────
  const selectNode = async (node) => {
    setSelNode(node)
    if (node.type === 'file' && selPart) {
      setLoading(true)
      try {
        // Construir ruta del nodo (aproximada desde el árbol)
        const data = await getFileContent(selPart.id, '/' + node.name)
        setFileContent(data.content)
      } catch { setFileContent(null) }
      setLoading(false)
    }
  }

  return (
    <div className="explorer-layout full-height">
      {/* Sidebar */}
      <div className="explorer-sidebar">
        <button className="btn btn-secondary btn-sm" style={{width:'100%', marginBottom:'12px'}}
                onClick={loadMounted}>
          ↺ Actualizar
        </button>

        {/* Discos */}
        <div className="sidebar-section">
          <div className="sidebar-title">💾 Discos</div>
          {disks.length === 0
            ? <div className="text-dim text-xs" style={{padding:'0 4px'}}>Sin discos</div>
            : disks.map((d, i) => (
              <div key={i} className="disk-item text-sm">
                {d.name}
                <div className="text-xs text-dim">{(d.size/1024/1024).toFixed(1)}MB · fit:{d.fit}</div>
              </div>
            ))
          }
        </div>

        {/* Particiones montadas */}
        <div className="sidebar-section">
          <div className="sidebar-title">🗂 Particiones</div>
          {mounted.length === 0
            ? <div className="text-dim text-xs" style={{padding:'0 4px'}}>Sin particiones montadas</div>
            : mounted.map((mp, i) => (
              <div key={i}
                   className={`partition-item text-sm ${selPart?.id === mp.id ? 'active' : ''}`}
                   onClick={() => selectPartition(mp)}>
                <strong>{mp.id}</strong> · {mp.name}
                <span className={`partition-badge ${mp.fs === 3 ? 'badge-ext3' : 'badge-ext2'}`}>
                  EXT{mp.fs || 2}
                </span>
                <div className="text-xs text-dim">{(mp.size/1024/1024).toFixed(1)}MB</div>
              </div>
            ))
          }
        </div>
      </div>

      {/* Panel principal */}
      <div className="explorer-main">
        {!selPart ? (
          <div style={{textAlign:'center', marginTop:'80px', color:'var(--text-dim)'}}>
            <div style={{fontSize:'48px', marginBottom:'16px'}}>💽</div>
            <div>Selecciona una partición del panel izquierdo</div>
          </div>
        ) : (
          <>
            {/* Header */}
            <div className="flex gap-3" style={{marginBottom:'16px', alignItems:'center'}}>
              <h2 style={{fontSize:'18px', fontWeight:700}}>
                {selPart.name}
                <span className={`partition-badge ${selPart.fs===3?'badge-ext3':'badge-ext2'}`}
                      style={{marginLeft:'10px'}}>
                  EXT{selPart.fs||2}
                </span>
              </h2>
              <div style={{flex:1}}/>
              {/* Tabs */}
              <div className="tabs">
                <button className={`tab ${tab==='explorer'?'active':''}`}
                        onClick={() => setTab('explorer')}>Archivos</button>
                {selPart.fs === 3 && (
                  <button className={`tab ${tab==='journal'?'active':''}`}
                          onClick={() => setTab('journal')}>Journal</button>
                )}
              </div>
            </div>

            {loading && <div className="text-dim">Cargando...</div>}

            {tab === 'explorer' && !loading && (
              <div style={{display:'grid', gridTemplateColumns:'1fr 1fr', gap:'16px'}}>
                {/* Árbol de archivos */}
                <div className="card">
                  <div className="card-title">📁 Sistema de Archivos</div>
                  <div className="file-tree">
                    {fileTree
                      ? <TreeNode node={fileTree} onSelect={selectNode} selected={selNode} />
                      : <div className="text-dim text-sm">Sin datos (¿está formateada?)</div>
                    }
                  </div>
                </div>

                {/* Detalle del nodo seleccionado */}
                <div className="card">
                  <div className="card-title">
                    {selNode ? `📄 ${selNode.name}` : 'Selecciona un archivo'}
                  </div>
                  {selNode && (
                    <>
                      <div style={{marginBottom:'12px'}}>
                        <span className="text-xs text-dim">Tipo: </span>
                        <span className="text-sm">{selNode.type === 'folder' ? '📁 Carpeta' : '📄 Archivo'}</span>
                        <span className="text-xs text-dim" style={{marginLeft:'12px'}}>Permisos: </span>
                        <span className="text-sm" style={{fontFamily:'var(--font-mono)'}}>{selNode.perm}</span>
                        <span className="text-xs text-dim" style={{marginLeft:'12px'}}>Tamaño: </span>
                        <span className="text-sm">{selNode.size}B</span>
                      </div>
                      {fileContent !== null && (
                        <div className="file-viewer">{fileContent || '(archivo vacío)'}</div>
                      )}
                    </>
                  )}
                </div>
              </div>
            )}

            {tab === 'journal' && journal && (
              <div>
                <div className="card-title" style={{marginBottom:'12px'}}>
                  📋 Journal EXT3 — {journal.count || 0} entrada(s)
                </div>
                {(!journal.entries || journal.entries.length === 0)
                  ? <div className="text-dim">Journal vacío</div>
                  : journal.entries.map((e, i) => (
                    <div key={i} className="journal-entry">
                      <span className="j-op">{e.op}</span>
                      <span className="j-path">{e.path}</span>
                      <span className="j-date">{e.date}</span>
                    </div>
                  ))
                }
              </div>
            )}
          </>
        )}
      </div>
    </div>
  )
}
