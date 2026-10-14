// What the pet says about a tool call: a short narration line, and a fuller
// summary for an approval.

const basename = (p: unknown) => String(p ?? '').split('/').filter(Boolean).pop() ?? ''

const clip = (s: string, n: number) => (s.length > n ? s.slice(0, n - 1) + '…' : s)

const field = (input: unknown, key: string): string => {
  const v = input && typeof input === 'object' ? (input as Record<string, unknown>)[key] : undefined
  return typeof v === 'string' ? v : ''
}

const host = (url: string) => {
  try {
    return new URL(url).host
  } catch {
    return url
  }
}

/** `mcp__server__tool` → `server: tool`; anything else unchanged. */
export function toolName(tool: string): string {
  const m = /^mcp__(.+?)__(.+)$/.exec(tool)
  return m ? `${m[1]}: ${m[2]}` : tool
}

/** One line for the narration under the pet. */
export function narrate(tool: string, input: unknown): string {
  switch (tool) {
    case 'Read':
      return `Reading ${basename(field(input, 'file_path'))}`
    case 'Edit':
    case 'MultiEdit':
      return `Editing ${basename(field(input, 'file_path'))}`
    case 'Write':
      return `Writing ${basename(field(input, 'file_path'))}`
    case 'NotebookEdit':
      return `Editing ${basename(field(input, 'notebook_path'))}`
    case 'Bash':
      return clip(`$ ${field(input, 'command').split('\n')[0]}`, 60)
    case 'Grep':
      return clip(`Searching for ${field(input, 'pattern')}`, 60)
    case 'Glob':
      return clip(`Finding ${field(input, 'pattern')}`, 60)
    case 'WebFetch':
      return `Reading ${host(field(input, 'url'))}`
    case 'WebSearch':
      return clip(`Searching the web: ${field(input, 'query')}`, 60)
    case 'Agent':
    case 'Task':
      return clip(`Delegating: ${field(input, 'description') || 'a subagent'}`, 60)
    case 'TodoWrite':
      return 'Planning'
    case 'AskUserQuestion':
      return 'Has a question for you'
    case 'Skill':
      return `Using ${field(input, 'skill') || 'a skill'}`
    default:
      return clip(toolName(tool), 60)
  }
}

/** The approval's body: what exactly is being asked. */
export function summarize(tool: string, input: unknown): string {
  switch (tool) {
    case 'Bash':
      return clip(field(input, 'command').replace(/\s+/g, ' '), 140)
    case 'Edit':
    case 'MultiEdit':
    case 'Write':
    case 'Read':
      return clip(field(input, 'file_path'), 140)
    case 'WebFetch':
      return clip(field(input, 'url'), 140)
    default: {
      let json = ''
      try {
        json = JSON.stringify(input)
      } catch {
        json = ''
      }
      return clip(json === '{}' ? toolName(tool) : json, 140)
    }
  }
}

/** A stable palette index (0..7) for a name. */
export function colorFor(name: string): number {
  let h = 0
  for (let i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) >>> 0
  return h % 8
}

export function duration(ms: number): string {
  const s = Math.round(ms / 1000)
  if (s < 60) return `${s}s`
  return `${Math.floor(s / 60)}m ${String(s % 60).padStart(2, '0')}s`
}

/** `claude-opus-5-5[1m]` → `opus 5.5`; anything unrecognised, clipped. */
export function shortModel(model: string): string {
  const bare = model.replace(/^claude-/, '').replace(/\[.*?\]/g, '').replace(/-\d{8}$/, '')
  const m = /^([a-z]+)-(\d+)-(\d+)/.exec(bare)
  return clip(m ? `${m[1]} ${m[2]}.${m[3]}` : bare, 10)
}

const FILLER = new Set(['agent', 'engineer', 'purpose', 'palesa', 'code', 'claude'])

/** An agent type as a short name under its pet: `palesa-frontend-engineer` → `frontend`. */
export function agentName(type: string): string {
  const words = type.split(/[-_:\s]+/).filter(Boolean)
  const kept = words.filter(w => !FILLER.has(w.toLowerCase()))
  return clip((kept[0] ?? words[0] ?? 'agent'), 11)
}
