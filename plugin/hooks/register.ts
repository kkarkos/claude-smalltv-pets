// smalltv-pet: mirrors each Claude Code session onto a desk pet running the
// claude-smalltv-pets firmware, and lets the pet's button answer permission
// prompts.
//
// Hooks only record what happened into the session's snapshot; a timer sends
// the snapshot when it changed, so no hook waits on the network except the
// approval, whose whole point is to wait for the button.
import type { HttpInit, HttpResponse, Register, SessionUsage } from 'claude-code'

import { agentName, colorFor, duration, narrate, shortModel, summarize, toolName } from './describe'

type PetState = 'idle' | 'thinking' | 'working' | 'waiting' | 'done' | 'error'

type Helper = { name: string; color: number; body: number; label: string; state: PetState; isDone: boolean; doneAt: number }

const FLUSH_MS = 200
const HEARTBEAT_MS = 60_000
const HELPER_LINGER_MS = 15_000

const clip = (s: string, n: number) => (s.length > n ? s.slice(0, n - 1) + '…' : s)
const basename = (p: string) => p.split('/').filter(Boolean).pop() ?? 'claude'
const POLITE = /\b(please|thanks|thank you|thx|danke|bitte)\b/i

export const register: Register = (on, options) => {
  const base = String(options.deviceUrl || 'http://claude-pet.local').replace(/\/+$/, '')
  const approvalSeconds = Math.max(0, Number(options.approvalSeconds ?? 20))
  // The pet holds approval polls open on a server of their own, port 81.
  const waitBase = (() => {
    try {
      const url = new URL(base)
      url.port = '81'
      return url.origin
    } catch {
      return base
    }
  })()

  let sid = ''
  let name = ''
  // Left unset, the pet gives each session a colour and body no other one has.
  let color: number | undefined
  let body: number | undefined
  let state: PetState = 'idle'
  let label = ''
  let ctx: number | null = null
  let five: number | null = null
  let week: number | null = null
  const helpers = new Map<string, Helper>()
  // Agent calls the lead made, waiting for their SubagentStart to claim them:
  // that is where a helper's first line ("Find the touch code") comes from.
  const delegated: { type: string; description: string }[] = []
  let model = ''
  let effort = ''
  let pats = 0
  let toolsInFlight = 0
  let isDirty = false
  let isSending = false
  let isOnline = false
  let lastError = ''
  let isApprovalOn = true

  const set = (next: PetState, text?: string) => {
    state = next
    if (text !== undefined) label = clip(text, 90)
    isDirty = true
  }

  const snapshot = () => ({
    sid,
    name,
    ...(color === undefined ? {} : { color }),
    ...(body === undefined ? {} : { body }),
    state,
    label,
    ctx,
    five,
    week,
    model,
    effort,
    pats,
    // Busy helpers first, so the ones the pet has room for are the working ones.
    helpers: [...helpers.entries()]
      .sort(([, a], [, b]) => Number(a.isDone) - Number(b.isDone))
      .slice(0, 5)
      .map(([id, h]) => ({
        id: id.slice(0, 20),
        name: h.name,
        color: h.color,
        body: h.body,
        state: h.state,
        label: clip(h.label, 46),
        done: h.isDone,
      })),
  })

  const noteEffort = (level: string | undefined) => {
    if (level && level !== effort) {
      effort = level
      isDirty = true
    }
  }

  type Fetch = (url: string, init?: HttpInit) => Promise<HttpResponse>

  const post = async (fetch: Fetch, path: string, body: unknown) => {
    try {
      const res = await fetch(base + path, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify(body),
      })
      isOnline = res.ok
      lastError = res.ok ? '' : `HTTP ${res.status}: ${res.text.slice(0, 80)}`
      return res
    } catch (err) {
      isOnline = false
      lastError = String(err)
      return undefined
    }
  }

  const pruneHelpers = () => {
    const now = Date.now()
    for (const [id, h] of helpers)
      if (h.isDone && now - h.doneAt > HELPER_LINGER_MS) {
        helpers.delete(id)
        isDirty = true
      }
  }

  const flush = async (fetch: Fetch) => {
    pruneHelpers()
    if (!sid || !isDirty || isSending) return
    isDirty = false
    isSending = true
    try {
      await post(fetch, '/api/session', snapshot())
    } finally {
      isSending = false
    }
  }

  const refreshUsage = async (read: () => Promise<SessionUsage>) => {
    try {
      const usage = await read()
      ctx = usage.context.percent === undefined ? null : Math.round(usage.context.percent)
      for (const limit of usage.rateLimits) {
        if (limit.kind === 'five_hour') five = Math.round(limit.percentUsed)
        if (limit.kind === 'seven_day') week = Math.round(limit.percentUsed)
      }
      isDirty = true
    } catch {
      // usage is a nicety; the pet shows "unknown" bars without it
    }
  }

  // Long-polls the pet until the button answers or `ms` runs out. Each poll
  // is a $ call in flight, which the hook's own time budget does not count.
  const waitForDecision = async (fetch: Fetch, sleep: (ms: number) => Promise<void>, id: string, ms: number) => {
    const until = Date.now() + ms
    while (Date.now() < until) {
      const startedAt = Date.now()
      let decision = 'pending'
      try {
        const res = await fetch(`${waitBase}/api/approval?id=${encodeURIComponent(id)}&wait=8`)
        decision = JSON.parse(res.text).decision ?? 'pending'
      } catch {
        return 'error'
      }
      if (decision !== 'pending') return decision
      // A pet that answers at once rather than holding the request: poll gently.
      if (Date.now() - startedAt < 1000) await sleep(250)
    }
    return 'expired'
  }

  const askOnPet = async (
    fetch: Fetch,
    sleep: (ms: number) => Promise<void>,
    ask: { tool: string; summary: string; seconds: number },
  ) => {
    const id = Math.random().toString(36).slice(2, 12)
    const opened = await post(fetch, '/api/approve', { sid, id, tool: ask.tool, summary: ask.summary, timeout: ask.seconds })
    if (!opened?.ok) return { id, decision: 'offline' }
    return { id, decision: await waitForDecision(fetch, sleep, id, ask.seconds * 1000 + 2000) }
  }

  on('session.start', async ($, e, next) => {
    const result = await next(e)
    sid = await $.session.id()
    name = basename(e.cwd)
    const storedColor = await $.store.get(`color:${name}`)
    color = typeof storedColor === 'number' ? storedColor : undefined
    const storedBody = await $.store.get(`body:${name}`)
    body = typeof storedBody === 'number' ? storedBody : undefined
    isApprovalOn = (await $.store.get('approvals')) !== false
    // Nice to have: a lookup that fails must not cost the session its pet.
    model = shortModel(await $.session.model().catch(() => ''))
    noteEffort(await $.env.get('CLAUDE_EFFORT').catch(() => undefined))
    await $.command.register({
      name: 'pet',
      description: 'Desk pet: status, test, ask, approvals on|off, color 0-7|auto, body 0-3|auto, brightness 0-100',
      argumentHint: '[status|test|ask|approvals on|off|color N|body N|brightness N]',
    })
    set('idle', 'Ready')
    await refreshUsage(() => $.session.usage())
    $.clock.every(FLUSH_MS, () => void flush((url, init) => $.http.fetch(url, init)))
    $.clock.every(HEARTBEAT_MS, () => {
      isDirty = true
      void refreshUsage(() => $.session.usage())
    })
    return result
  })

  on('session.end', async ($, e, next) => {
    if (sid) await post((url, init) => $.http.fetch(url, init), '/api/end', { sid })
    const result = await next(e)
    // A /clear ends the conversation but not the process: it goes on under a
    // new id with no session.start, so the pet follows it there.
    if (e.reason === 'clear') {
      sid = await $.session.id()
      helpers.clear()
      set('idle', 'Cleared')
    }
    return result
  })

  on('turn.start', async ($, e, next) => {
    set('thinking', 'Thinking…')
    model = shortModel(await $.session.model().catch(() => model))
    return next(e)
  })

  on('prompt.submit', ($, e, next) => {
    if (POLITE.test(e.text)) {
      pats += 1
      isDirty = true
    }
    return next(e)
  })

  on('classic.PostToolUse', ($, e, next) => {
    noteEffort(e.effort?.level)
    return next(e)
  })

  on('classic.Stop', ($, e, next) => {
    noteEffort(e.effort?.level)
    return next(e)
  })

  on('tool.call', async ($, e, next) => {
    const tool = String(e.tool)
    // A subagent's call is its helper's line; the main narration stays the lead's.
    if (e.agentId) {
      const helper = helpers.get(e.agentId)
      if (helper && !helper.isDone) {
        helper.label = narrate(tool, e)
        helper.state = 'working'
        isDirty = true
      }
      return next(e)
    }
    if (tool === 'Agent' || tool === 'Task') {
      const input = e as unknown as { subagent_type?: string; description?: string }
      delegated.push({ type: input.subagent_type ?? 'general-purpose', description: input.description ?? '' })
    }
    toolsInFlight += 1
    set(tool === 'AskUserQuestion' ? 'waiting' : 'working', narrate(tool, e))
    try {
      return await next(e)
    } finally {
      toolsInFlight -= 1
      if (toolsInFlight === 0 && (state === 'working' || state === 'waiting')) set('thinking')
    }
  }).catch(($, e, next) => next(e))

  on('turn.complete', async ($, e, next) => {
    const result = await next(e)
    if (e.agentId) return result
    const took = duration(e.durationMs)
    if (e.reason === 'answer') set('done', `Done in ${took}`)
    else if (e.reason === 'aborted') set('idle', 'Interrupted')
    else set('error', e.reason === 'refusal' ? 'Refused' : 'Stopped on an error')
    void refreshUsage(() => $.session.usage())
    return result
  })

  on('classic.SubagentStart', ($, e, next) => {
    const claimed = delegated.findIndex(d => d.type === e.agent_type)
    const description = claimed >= 0 ? delegated.splice(claimed, 1)[0]?.description ?? '' : ''
    helpers.set(e.agent_id, {
      name: agentName(e.agent_type),
      color: colorFor(e.agent_type),
      body: colorFor(`${e.agent_type}#body`) % 4,
      label: description,
      state: 'working',
      isDone: false,
      doneAt: 0,
    })
    isDirty = true
    return next(e)
  })

  on('classic.SubagentStop', ($, e, next) => {
    const helper = helpers.get(e.agent_id)
    if (helper) {
      helper.isDone = true
      helper.state = 'done'
      helper.label = 'done'
      helper.doneAt = Date.now()
    }
    noteEffort(e.effort?.level)
    isDirty = true
    return next(e)
  })

  on('classic.Notification', ($, e, next) => {
    if (e.notification_type === 'permission_prompt' && state !== 'waiting') set('waiting', 'Needs your approval')
    if (e.notification_type === 'elicitation_dialog') set('waiting', e.message)
    if (e.notification_type === 'idle_prompt' && state === 'done') set('idle', 'Your turn')
    return next(e)
  })

  // The button answers first; when nobody presses it in time the terminal's
  // own dialog takes over, as if the pet were not there.
  on('classic.PermissionRequest', async ($, e, next) => {
    if (!isApprovalOn || approvalSeconds <= 0 || !isOnline) return next(e)
    const tool = toolName(String(e.tool_name))
    const before = label
    set('waiting', `Approve ${tool}?`)
    await flush((url, init) => $.http.fetch(url, init))
    $.ui.status(`smalltv-pet: tap the pet to allow, hold to deny (${approvalSeconds}s)`)
    const { id, decision } = await askOnPet(
      (url, init) => $.http.fetch(url, init),
      ms => $.clock.sleep(ms),
      { tool, summary: summarize(String(e.tool_name), e.tool_input), seconds: approvalSeconds },
    )
    $.ui.status(undefined)
    if (decision === 'allow') {
      set('working', before)
      return { decision: { behavior: 'allow' as const } }
    }
    if (decision === 'deny') {
      set('thinking', `Denied ${tool}`)
      return { decision: { behavior: 'deny' as const, message: 'Denied with the button on the desk pet.' } }
    }
    if (decision !== 'offline') await post((url, init) => $.http.fetch(url, init), '/api/cancel', { id })
    set('waiting', 'Approve in the terminal')
    return next(e)
  }).catch(($, e, next) => next(e))

  on('command.run', { command: 'pet' }, async ($, e) => {
    const [sub = 'status', arg = ''] = e.args.trim().split(/\s+/).filter(Boolean)

    if (sub === 'status') {
      let info = ''
      try {
        const res = await $.http.fetch(`${base}/api/info`)
        isOnline = res.ok
        info = res.ok ? res.text : `HTTP ${res.status}`
      } catch (err) {
        isOnline = false
        info = String(err)
      }
      return {
        text: [
          `Pet: ${base} (${isOnline ? 'online' : 'offline'})`,
          `Desk approvals: ${isApprovalOn && approvalSeconds > 0 ? `on, ${approvalSeconds}s` : 'off'}`,
          `This session: ${name}, colour ${color ?? 'auto'}, body ${body ?? 'auto'}, ${state}${label ? ` (${label})` : ''}`,
          isOnline ? `Device: ${info}` : `Last error: ${lastError || info}`,
        ].join('\n'),
      }
    }

    if (sub === 'test') {
      const steps: [number, () => void][] = [
        [0, () => set('thinking', 'Thinking…')],
        [1500, () => set('working', 'Editing register.ts')],
        [2500, () => {
          helpers.set('demo-a', { name: 'Explore', color: 1, body: 1, label: 'Reading touch.cpp', state: 'working', isDone: false, doneAt: 0 })
          helpers.set('demo-b', { name: 'reviewer', color: 2, body: 2, label: 'Checking the diff', state: 'working', isDone: false, doneAt: 0 })
          helpers.set('demo-c', { name: 'frontend', color: 4, body: 3, label: 'Editing Pet.vue', state: 'waiting', isDone: false, doneAt: 0 })
          set('working', 'Delegating: two helpers')
        }],
        [5000, () => set('working', '$ bun test --watch=false and a long command that scrolls')],
        [8000, () => {
          for (const h of helpers.values()) Object.assign(h, { isDone: true, state: 'done', label: 'done', doneAt: Date.now() })
          set('waiting', 'Has a question for you')
        }],
        [11000, () => set('error', 'Stopped on an error')],
        [13000, () => set('done', 'Done in 13s')],
      ]
      for (const [at, step] of steps) $.clock.after(at, step)
      return { text: 'Playing a 15-second demo on the pet.' }
    }

    if (sub === 'ask') {
      $.ui.status('smalltv-pet: tap the pet to allow, hold to deny')
      const { decision } = await askOnPet((url, init) => $.http.fetch(url, init), ms => $.clock.sleep(ms), {
        tool: 'Test',
        summary: 'A test approval from /pet ask. Tap to allow, hold to deny.',
        seconds: 15,
      })
      $.ui.status(undefined)
      return { text: `The pet answered: ${decision}` }
    }

    if (sub === 'approvals') {
      isApprovalOn = arg !== 'off'
      await $.store.set('approvals', isApprovalOn)
      return { text: `Desk approvals ${isApprovalOn ? 'on' : 'off'}.` }
    }

    if (sub === 'color' || sub === 'body') {
      const max = sub === 'color' ? 7 : 3
      const what = sub === 'color' ? 'colour' : 'body'
      if (arg === 'auto') {
        await $.store.delete(`${sub}:${name}`)
        return { text: `${name} gets its ${what} from the pet again (from the next session on).` }
      }
      const n = Number(arg)
      if (!Number.isInteger(n) || n < 0 || n > max) return { text: `Give a ${what} from 0 to ${max}, or auto.` }
      if (sub === 'color') color = n
      else body = n
      isDirty = true
      await $.store.set(`${sub}:${name}`, n)
      return { text: `${name} has ${what} ${n} from now on.` }
    }

    if (sub === 'brightness') {
      const n = Number(arg)
      if (!Number.isFinite(n) || n < 0 || n > 100) return { text: 'Give a brightness from 0 to 100.' }
      const res = await post((url, init) => $.http.fetch(url, init), '/api/config', { brightness: Math.round(n) })
      return { text: res?.ok ? `Brightness ${Math.round(n)}%.` : `The pet did not answer (${lastError}).` }
    }

    return { text: 'Usage: /pet [status|test|ask|approvals on|off|color 0-7|auto|body 0-3|auto|brightness 0-100]' }
  })
}
