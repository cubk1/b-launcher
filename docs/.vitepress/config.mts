import { defineConfig, type DefaultTheme } from 'vitepress'
import { existsSync, readdirSync, readFileSync, statSync, utimesSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const configFile = fileURLToPath(import.meta.url)
const root = fileURLToPath(new URL('..', import.meta.url))
const skipped = new Set(['node_modules', 'public'])
const known = new Map<string, { fallback: string; key: string }>()

type Page = { title: string; order: number }
type Node = { name: string; page: Page; item: DefaultTheme.SidebarItem }

function read(file: string, fallback: string): Page {
  const text = readFileSync(file, 'utf8').replace(/^\uFEFF/, '')
  const front = /^---\r?\n([\s\S]*?)\r?\n---/.exec(text)
  const body = front ? text.slice(front[0].length) : text
  const field = (key: string) =>
    new RegExp(`^${key}:[ \\t]*(.+?)[ \\t]*$`, 'm').exec(front?.[1] ?? '')?.[1].replace(/^(['"])(.*)\1$/, '$2')
  const heading = /^#[ \t]+(.+?)[ \t]*#*[ \t]*$/m.exec(body)?.[1]
  const order = Number(field('order'))
  const page = { title: field('title') ?? heading ?? fallback, order: Number.isFinite(order) ? order : Infinity }
  known.set(resolve(file), { fallback, key: `${page.title}\n${page.order}` })
  return page
}

function changed(file: string) {
  const before = known.get(resolve(file))
  if (!before) {
    return false
  }
  const page = read(file, before.fallback)
  return `${page.title}\n${page.order}` !== before.key
}

function refresh() {
  const now = new Date()
  utimesSync(configFile, now, now)
}

function sorted(nodes: Node[]) {
  return nodes.sort((a, b) => a.page.order - b.page.order || a.name.localeCompare(b.name, 'zh-CN', { numeric: true }))
}

function hidden(name: string) {
  return name.startsWith('.') || name.startsWith('_') || skipped.has(name)
}

function folder(dir: string, base: string, name: string): Node | null {
  const items = scan(dir, base)
  const index = join(dir, 'index.md')
  const page = existsSync(index) ? read(index, name) : { title: name, order: Infinity }
  if (items.length === 0 && !existsSync(index)) {
    return null
  }
  return {
    name,
    page,
    item: {
      text: page.title,
      link: existsSync(index) ? base : undefined,
      collapsed: base.split('/').length > 3 ? false : undefined,
      items: items.map((node) => node.item)
    }
  }
}

function scan(dir: string, base: string): Node[] {
  const nodes: Node[] = []
  for (const name of readdirSync(dir)) {
    const full = join(dir, name)
    if (hidden(name)) {
      continue
    }
    if (statSync(full).isDirectory()) {
      const node = folder(full, `${base}${name}/`, name)
      if (node) {
        nodes.push(node)
      }
    } else if (name.endsWith('.md') && name !== 'index.md') {
      const stem = name.slice(0, -3)
      const page = read(full, stem)
      nodes.push({ name: stem, page, item: { text: page.title, link: `${base}${stem}` } })
    }
  }
  return sorted(nodes)
}

function firstLink(item: DefaultTheme.SidebarItem): string | undefined {
  return item.link ?? item.items?.map(firstLink).find(Boolean)
}

function tree() {
  const nav: DefaultTheme.NavItem[] = []
  const sidebar: DefaultTheme.SidebarMulti = {}
  for (const node of scan(root, '/')) {
    const link = firstLink(node.item)
    if (!link) {
      continue
    }
    if (node.item.items) {
      const base = `/${node.name}/`
      nav.push({ text: node.page.title, link, activeMatch: `^${base}` })
      sidebar[base] = [node.item]
    } else {
      nav.push({ text: node.page.title, link })
    }
  }
  return { nav, sidebar }
}

const { nav, sidebar } = tree()
const empty = known.size === 0 && !existsSync(join(root, 'index.md'))

export default defineConfig({
  lang: 'zh-CN',
  title: 'B Launcher',
  description: '轻量 Minecraft 启动器，使用纯 C++ 开发',
  cleanUrls: true,
  lastUpdated: true,
  sitemap: empty ? undefined : { hostname: 'https://b-docs.cubk.cn' },
  vite: {
    plugins: [
      {
        name: 'docs-tree',
        configureServer(server) {
          server.watcher
            .on('add', (file) => file.endsWith('.md') && refresh())
            .on('unlink', (file) => known.has(resolve(file)) && refresh())
            .on('change', (file) => changed(file) && refresh())
        }
      }
    ]
  },
  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: '/logo.svg' }]],
  themeConfig: {
    logo: '/logo.svg',
    nav,
    sidebar,
    socialLinks: [{ icon: 'github', link: 'https://github.com/cubk1/b-launcher' }],
    editLink: {
      pattern: 'https://github.com/cubk1/b-launcher/edit/master/docs/:path',
      text: '在 GitHub 上编辑此页'
    },
    search: {
      provider: 'local',
      options: {
        translations: {
          button: { buttonText: '搜索', buttonAriaLabel: '搜索' },
          modal: {
            displayDetails: '显示详情',
            resetButtonTitle: '清除',
            backButtonTitle: '关闭搜索',
            noResultsText: '没有找到结果',
            footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' }
          }
        }
      }
    },
    outline: { level: [2, 3], label: '本页目录' },
    docFooter: { prev: '上一页', next: '下一页' },
    lastUpdated: { text: '最后更新于' },
    notFound: { title: '页面不存在', quote: '这个地址没有对应的页面。', linkText: '返回首页' },
    darkModeSwitchLabel: '外观',
    lightModeSwitchTitle: '切换到浅色模式',
    darkModeSwitchTitle: '切换到深色模式',
    sidebarMenuLabel: '菜单',
    returnToTopLabel: '回到顶部',
    langMenuLabel: '语言',
    skipToContentLabel: '跳到正文'
  }
})
