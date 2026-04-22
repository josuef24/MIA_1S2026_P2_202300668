// ============================================================
// api.js — Llamadas al backend C++ desde el frontend
// ============================================================

// URL base del backend — cambiar por la IP del EC2 al desplegar
const BASE_URL = import.meta.env.VITE_API_URL || '/api'

// ── Ejecutar un comando ───────────────────────────────────
export async function executeCommand(command) {
  const res = await fetch(`${BASE_URL}/execute`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command })
  })
  return res.json()
}

// ── Ejecutar un script completo ────────────────────────────
export async function executeScript(script) {
  const res = await fetch(`${BASE_URL}/execute_script`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ script })
  })
  return res.json()
}

// ── Login ──────────────────────────────────────────────────
export async function apiLogin(user, pass, id) {
  const res = await fetch(`${BASE_URL}/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user, pass, id })
  })
  return res.json()
}

// ── Logout ─────────────────────────────────────────────────
export async function apiLogout() {
  const res = await fetch(`${BASE_URL}/logout`, { method: 'POST' })
  return res.json()
}

// ── Estado de sesión ────────────────────────────────────────
export async function getSession() {
  const res = await fetch(`${BASE_URL}/session`)
  return res.json()
}

// ── Particiones montadas ────────────────────────────────────
export async function getMounted() {
  const res = await fetch(`${BASE_URL}/mounted`)
  return res.json()
}

// ── Discos disponibles ──────────────────────────────────────
export async function getDisks() {
  const res = await fetch(`${BASE_URL}/disks`)
  return res.json()
}

// ── Árbol de archivos de una partición ────────────────────
export async function getFileTree(partId) {
  const res = await fetch(`${BASE_URL}/partition/${partId}/files`)
  return res.json()
}

// ── Contenido de un archivo ────────────────────────────────
export async function getFileContent(partId, path) {
  const res = await fetch(`${BASE_URL}/partition/${partId}/file?path=${encodeURIComponent(path)}`)
  return res.json()
}

// ── Journal de una partición EXT3 ─────────────────────────
export async function getJournal(partId) {
  const res = await fetch(`${BASE_URL}/partition/${partId}/journal`)
  return res.json()
}
