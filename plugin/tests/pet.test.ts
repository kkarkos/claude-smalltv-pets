import { expect, mock, test } from 'claude-code/testing'
import type { On } from 'claude-code'

type Post = { url: string; body: Record<string, unknown> }

// Stands in for the pet: records every POST and answers approval polls with
// whatever `answer` last set.
function fakePet(on: On) {
  const posts: Post[] = []
  let decision = 'pending'
  on('http.fetch', ($, e) => {
    if (e.init?.method === 'POST') posts.push({ url: e.url, body: JSON.parse(e.init.body ?? '{}') })
    const text = e.url.includes('/api/approval?') ? JSON.stringify({ decision }) : '{"ok":true}'
    return { value: { status: 200, ok: true, headers: {}, text } }
  })
  on('session.id', () => ({ value: 'session-1' }))
  on('session.model', () => ({ value: 'claude-opus-5-5[1m]' }))
  on('prompt.submit', ($, e) => ({ text: e.text }) as never)
  mock.env(on, { CLAUDE_EFFORT: 'high' })
  on('session.usage', () => ({
    value: { startedAt: 0, context: { window: 200000, percent: 42 }, rateLimits: [{ kind: 'five_hour', percentUsed: 30 }] },
  }))
  on('command.register', () => ({ value: undefined }))
  on('session.start', ($, e) => ({ cwd: e.cwd }))
  on('turn.complete', ($, e) => ({ text: e.answer }))
  on('ui.status', () => ({ value: undefined }))
  on('classic.PermissionRequest', () => ({}))
  mock.store(on)
  const lastSession = () => posts.filter(p => p.url.endsWith('/api/session')).at(-1)?.body
  return { posts, lastSession, answer: (d: string) => (decision = d) }
}

test('narrates the tool call and sends usage', async ($, on) => {
  const clock = mock.clock(on)
  const pet = fakePet(on)
  on('tool.call', () => ({ result: 'file contents' }) as never)

  await $.session.start({ cwd: '/Users/me/projects/demo', surface: 'terminal', isInteractive: true })
  await $.tool.call({ tool: 'Read', file_path: '/Users/me/projects/demo/src/foo.ts' } as never)
  await clock.advance(250)

  expect(pet.lastSession()).toMatchObject({
    sid: 'session-1',
    name: 'demo',
    label: 'Reading foo.ts',
    state: 'thinking',
    ctx: 42,
    five: 30,
  })
})

test('a tap on the pet allows the permission request', async ($, on) => {
  const clock = mock.clock(on)
  const pet = fakePet(on)
  await $.session.start({ cwd: '/tmp/demo', surface: 'terminal', isInteractive: true })
  await clock.advance(250)

  pet.answer('allow')
  const result = await $.classic.PermissionRequest({ tool_name: 'Bash', tool_input: { command: 'git push origin main' } })

  expect(result.decision).toEqual({ behavior: 'allow' })
  const asked = pet.posts.find(p => p.url.endsWith('/api/approve'))
  expect(asked?.body).toMatchObject({ tool: 'Bash', summary: 'git push origin main' })
})

test('a hold on the pet denies it', async ($, on) => {
  const clock = mock.clock(on)
  const pet = fakePet(on)
  await $.session.start({ cwd: '/tmp/demo', surface: 'terminal', isInteractive: true })
  await clock.advance(250)

  pet.answer('deny')
  const result = await $.classic.PermissionRequest({ tool_name: 'Bash', tool_input: { command: 'rm -rf build' } })

  expect(result.decision).toMatchObject({ behavior: 'deny' })
})

test('no answer in time leaves it to the terminal dialog', async ($, on) => {
  const clock = mock.clock(on)
  const pet = fakePet(on)
  await $.session.start({ cwd: '/tmp/demo', surface: 'terminal', isInteractive: true })
  await clock.advance(250)

  pet.answer('expired')
  const result = await $.classic.PermissionRequest({ tool_name: 'Write', tool_input: { file_path: '/tmp/x' } })

  expect(result.decision).toBeUndefined()
  expect(pet.posts.some(p => p.url.endsWith('/api/cancel'))).toBe(true)
})

test('subagents become named helpers with their own line', async ($, on) => {
  const clock = mock.clock(on)
  const pet = fakePet(on)
  on('tool.call', () => ({ result: 'ok' }) as never)
  on('classic.SubagentStart', () => ({}))
  await $.session.start({ cwd: '/tmp/demo', surface: 'terminal', isInteractive: true })

  await $.tool.call({ tool: 'Agent', subagent_type: 'Explore', description: 'Find the touch code', prompt: '…' } as never)
  await $.classic.SubagentStart({ agent_id: 'agent-1', agent_type: 'Explore' })
  await clock.advance(250)
  expect(pet.lastSession()).toMatchObject({
    model: 'opus 5.5',
    effort: 'high',
    helpers: [{ id: 'agent-1', name: 'Explore', label: 'Find the touch code', state: 'working' }],
  })

  await $.tool.call({ tool: 'Grep', pattern: 'touchRead', agentId: 'agent-1' } as never)
  await clock.advance(250)
  expect(pet.lastSession()?.helpers).toMatchObject([{ label: 'Searching for touchRead' }])
})
