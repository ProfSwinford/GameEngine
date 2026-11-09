/* ============================================================================
   Engine2D scripting reference - the renderer.

   No framework. The whole reference is one data object (api-data.js, generated
   from the headers) plus the hand-written guide pages, which live as <template>
   elements in the HTML so they can be authored as real markup.

   ROUTING is on the hash, which is the only thing that survives being embedded:
       #                    home
       #index               the A-Z class index
       #module-physics      one module overview
       #guide-lifecycle     a hand-written page
       #Entity              a class page
       #Entity-Find         a class page, scrolled to one member
   Type names never contain a hyphen, so the first segment is unambiguous.
   ========================================================================= */

(function () {
    'use strict';

    var API = window.ENGINE_API || null;
    /* TYPES is keyed by ROUTING SLUG and BYNAME by display name. They differ
       for the grouping pages - "Vec2 operators", "ScriptRegistry::Entry" -
       which read correctly as a title and are unusable in a URL fragment:
       only a plain #name of letters, digits and hyphens survives being
       embedded, and anything else arrives percent-encoded and resolves to
       nothing. */
    var TYPES = Object.create(null);
    var BYNAME = Object.create(null);
    var ORDER = [];
    var MODULES = [];
    var SEARCH = [];

    /* ------------------------------------------------------------- escaping */

    function esc(s) {
        return String(s == null ? '' : s)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    }

    function attr(s) { return esc(s).replace(/'/g, '&#39;'); }

    /* -------------------------------------------------- C++ syntax colouring */

    var KEYWORDS = ('alignas auto bool break case catch char char8_t class concept const consteval ' +
        'constexpr constinit const_cast continue co_await decltype default delete do double ' +
        'dynamic_cast else enum explicit extern false final float for friend goto if inline int long ' +
        'mutable namespace new noexcept nullptr operator override private protected public ' +
        'reinterpret_cast requires return short signed sizeof static static_assert static_cast ' +
        'struct switch template this throw true try typedef typename union unsigned using virtual ' +
        'void volatile while').split(' ');

    var EXTRA_TYPES = ('std size_t ptrdiff_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t ' +
        'int32_t int64_t string string_view vector optional array span unique_ptr shared_ptr ' +
        'function map unordered_map set pair tuple nullopt_t byte filesystem path ostream').split(' ');

    /* Object.create(null) and not {}: these are consulted with ARBITRARY words out of
       code examples, and a bare object answers truthily for "constructor",
       "toString" and the rest of Object.prototype. The member grouper below hit
       exactly that - every class with a constructor failed to render. */
    var KW_SET = Object.create(null), TY_SET = Object.create(null);
    KEYWORDS.forEach(function (k) { KW_SET[k] = 1; });
    EXTRA_TYPES.forEach(function (k) { TY_SET[k] = 1; });

    var HL_RE = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(^[ \t]*#[A-Za-z_]+)|\b(0[xX][0-9a-fA-F]+|\d+\.?\d*[fF]?)\b|\b([A-Za-z_]\w*)\b/gm;

    function hl(src) {
        var out = '', last = 0, m;
        HL_RE.lastIndex = 0;
        while ((m = HL_RE.exec(src)) !== null) {
            out += esc(src.slice(last, m.index));
            last = m.index + m[0].length;
            if (m[1]) {
                out += '<span class="tok-com">' + esc(m[1]) + '</span>';
            } else if (m[2]) {
                out += '<span class="tok-str">' + esc(m[2]) + '</span>';
            } else if (m[3]) {
                out += '<span class="tok-pp">' + esc(m[3]) + '</span>';
            } else if (m[4]) {
                out += '<span class="tok-num">' + esc(m[4]) + '</span>';
            } else {
                var w = m[5];
                if (KW_SET[w]) out += '<span class="tok-kw">' + esc(w) + '</span>';
                else if (BYNAME[w] || TY_SET[w]) out += '<span class="tok-ty">' + esc(w) + '</span>';
                else out += esc(w);
            }
        }
        out += esc(src.slice(last));
        return out;
    }

    function codeBlock(src, cls) {
        return '<pre' + (cls ? ' class="' + cls + '"' : '') + '><code>' +
            hl(String(src == null ? '' : src).replace(/\s+$/, '')) + '</code></pre>';
    }

    /* --------------------------------------------- a very small markdown pass

       Descriptions are short. This handles paragraphs, blank-line breaks,
       bullet and numbered lists, pipe tables, inline code, bold, italic and
       links - and auto-links any `Type` in backticks that this reference has a
       page for, which is the thing that makes the prose navigable.          */

    function inline(s) {
        var parts = [];
        var txt = String(s == null ? '' : s);

        /* pull code spans out first so nothing else touches their contents */
        txt = txt.replace(/`([^`]+)`/g, function (_, code) {
            var key = code.replace(/^eng::/, '').replace(/\(\)$/, '').replace(/<.*>$/, '');
            var html;
            if (BYNAME[key]) {
                html = '<a href="#' + attr(BYNAME[key].slug) + '"><code>' + esc(code) + '</code></a>';
            } else {
                html = '<code>' + hl(code) + '</code>';
            }
            parts.push(html);
            return '\u0000' + (parts.length - 1) + '\u0000';
        });

        txt = esc(txt);
        txt = txt.replace(/\[([^\]]+)\]\(([^)]+)\)/g, function (_, label, href) {
            return '<a href="' + attr(href) + '">' + label + '</a>';
        });
        txt = txt.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
        txt = txt.replace(/(^|[\s(])\*([^*\n]+)\*(?=[\s).,;:]|$)/g, '$1<em>$2</em>');
        txt = txt.replace(/\u0000(\d+)\u0000/g, function (_, i) { return parts[+i]; });
        return txt;
    }

    function md(src) {
        if (!src) return '';
        var lines = String(src).replace(/\r/g, '').split('\n');
        var out = [], buf = [], list = null, table = null;

        function flushPara() {
            if (buf.length) { out.push('<p>' + inline(buf.join(' ')) + '</p>'); buf = []; }
        }
        function flushList() {
            if (list) { out.push('<' + list.tag + '>' + list.items.join('') + '</' + list.tag + '>'); list = null; }
        }
        function flushTable() {
            if (!table) return;
            var h = table.head.map(function (c) { return '<th>' + inline(c) + '</th>'; }).join('');
            var b = table.rows.map(function (r) {
                return '<tr>' + r.map(function (c) { return '<td>' + inline(c) + '</td>'; }).join('') + '</tr>';
            }).join('');
            out.push('<table><thead><tr>' + h + '</tr></thead><tbody>' + b + '</tbody></table>');
            table = null;
        }
        function flushAll() { flushPara(); flushList(); flushTable(); }

        for (var i = 0; i < lines.length; i++) {
            var ln = lines[i];
            var cells = null;

            /* A FENCED CODE BLOCK, TAKEN WHOLE. There was no branch for this
               at all, so every ``` block in a description fell through to the
               paragraph buffer below, which trims each line and joins them
               with spaces - and inline()'s /`([^`]+)`/ then opened its code
               span on the THIRD backtick of the fence. The result on the page
               was two literal backticks, one run-on line of code with its //
               comments swallowing whatever followed them, and two more
               backticks. The closing fence must be at least as long as the
               opening one, as CommonMark has it; the language tag is read and
               discarded, because hl() only knows C++. */
            var fence = ln.match(/^\s*(`{3,})\s*(\S*)\s*$/);
            if (fence) {
                flushAll();
                var fenceLen = fence[1].length, code = [];
                for (i++; i < lines.length; i++) {
                    var bare = lines[i].trim();
                    if (/^`+$/.test(bare) && bare.length >= fenceLen) { break; }
                    code.push(lines[i]);
                }
                out.push(codeBlock(code.join('\n')));
                continue;
            }

            if (/^\s*\|.*\|\s*$/.test(ln)) {
                cells = ln.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map(function (c) { return c.trim(); });
                if (/^[\s|:-]+$/.test(ln)) continue;
                flushPara(); flushList();
                if (!table) table = { head: cells, rows: [] };
                else table.rows.push(cells);
                continue;
            }
            flushTable();

            if (/^\s*$/.test(ln)) { flushPara(); flushList(); continue; }

            var m = ln.match(/^\s*[-*]\s+(.*)$/);
            if (m) {
                flushPara();
                if (!list || list.tag !== 'ul') { flushList(); list = { tag: 'ul', items: [] }; }
                list.items.push('<li>' + inline(m[1]) + '</li>');
                continue;
            }
            m = ln.match(/^\s*\d+[.)]\s+(.*)$/);
            if (m) {
                flushPara();
                if (!list || list.tag !== 'ol') { flushList(); list = { tag: 'ol', items: [] }; }
                list.items.push('<li>' + inline(m[1]) + '</li>');
                continue;
            }
            m = ln.match(/^\s*(#{2,4})\s+(.*)$/);
            if (m) {
                flushAll();
                var lvl = m[1].length;
                out.push('<h' + lvl + '>' + inline(m[2]) + '</h' + lvl + '>');
                continue;
            }

            flushList();
            buf.push(ln.trim());
        }
        flushAll();
        return out.join('\n');
    }

    /* ------------------------------------------------------------- the model */

    function buildModel() {
        if (!API) return;
        MODULES = API.modules || [];
        (API.types || []).forEach(function (t) {
            if (!t.slug) {
                t.slug = String(t.name).replace(/::/g, '-').replace(/[^A-Za-z0-9]+/g, '-')
                    .replace(/^-+|-+$/g, '') || 'type';
            }
            TYPES[t.slug] = t;
            BYNAME[t.name] = t;
            ORDER.push(t.slug);
        });

        /* search index: every type and every member */
        ORDER.forEach(function (n) {
            var t = TYPES[n];
            SEARCH.push({ name: t.name, owner: '', kind: t.kind, href: '#' + t.slug, lower: t.name.toLowerCase(), weight: 0 });
            (t.members || []).forEach(function (mem) {
                SEARCH.push({
                    name: mem.name,
                    owner: t.name,
                    kind: mem.kind,
                    href: '#' + t.slug + '-' + mem.slug,
                    lower: mem.name.toLowerCase(),
                    weight: 1
                });
            });
        });
        (API.guides || []).forEach(function (g) {
            SEARCH.push({ name: g.title, owner: 'guide', kind: 'page', href: '#guide-' + g.slug, lower: g.title.toLowerCase(), weight: 2 });
        });

        /* The modules list types by display name; everything downstream wants
           routing slugs, so translate once here - and SORT ALPHABETICALLY
           while we are at it.

           Sorting here rather than in the generator means the sidebar, the
           module overview pages and anything else reading mod.types are all
           in the same order, and stay that way if a type is ever added to
           api-data.js by hand. The audience dot on each row carries what the
           old grouped order used to say. */
        MODULES.forEach(function (mod) {
            mod.types = (mod.types || []).map(function (n) {
                return BYNAME[n] ? BYNAME[n].slug : null;
            }).filter(Boolean).sort(bySlugName);
        });
    }

    function byName(n) { return BYNAME[n] || null; }

    /* The one ordering the whole reference uses: display name, case-folded,
       compared plainly. The sidebar, the module pages, the A-Z index and the
       generator all call this comparison, so a type sits in the same place in
       all of them. localeCompare was tempting and weights spaces differently
       from the generator, which put "Collider functions" on opposite sides of
       "ColliderComponent" depending on which list you were looking at. */
    function bySlugName(a, b) {
        var x = TYPES[a].name.toLowerCase();
        var y = TYPES[b].name.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    }

    /* ---------------------------------------------------------------- the rail */

    var AUD_LABEL = { script: 'Scripting', system: 'Engine system', host: 'Host only' };

    function railHtml(active) {
        var h = [];

        h.push('<h2>Guides</h2><ul>');
        (API.guides || []).forEach(function (g) {
            var on = active === 'guide-' + g.slug ? ' class="on"' : '';
            h.push('<li><a href="#guide-' + attr(g.slug) + '"' + on + '>' + esc(g.title) + '</a></li>');
        });
        h.push('<li><a href="#index"' + (active === 'index' ? ' class="on"' : '') + '>All classes, A to Z</a></li>');
        h.push('</ul>');

        h.push('<h2>Reference</h2>');
        MODULES.forEach(function (mod) {
            var names = (mod.types || []).filter(function (n) { return TYPES[n]; });
            if (!names.length) return;
            var open = names.indexOf(active) >= 0 || active === 'module-' + mod.key;
            h.push('<details class="mod"' + (open ? ' open' : '') + ' data-mod="' + attr(mod.key) + '">');
            h.push('<summary>' + esc(mod.title) + '<span class="count">' + names.length + '</span></summary>');
            h.push('<ul>');
            h.push('<li><a href="#module-' + attr(mod.key) + '"' + (active === 'module-' + mod.key ? ' class="on"' : '') +
                '><span class="dot" style="background:var(--ink-3)"></span>Overview</a></li>');
            names.forEach(function (n) {
                var t = TYPES[n];
                h.push('<li><a href="#' + attr(n) + '"' + (active === n ? ' class="on"' : '') +
                    ' title="' + attr(t.summary) + '"><span class="dot ' + attr(t.audience) + '"></span>' +
                    '<span class="t-name">' + esc(t.name) + '</span></a></li>');
            });
            h.push('</ul></details>');
        });

        h.push('<h2>Legend</h2><ul>');
        ['script', 'system', 'host'].forEach(function (a) {
            h.push('<li><a href="#guide-reading" style="cursor:default"><span class="dot ' + a + '"></span>' +
                esc(AUD_LABEL[a]) + '</a></li>');
        });
        h.push('</ul>');

        return h.join('');
    }

    /* --------------------------------------------------------- member sorting

       Unity's order, which is the order somebody reads a class in: what it is,
       then how you make one, then what you can do with it, then the callbacks
       the engine sends you.                                                  */

    var SECTIONS = [
        { key: 'enum-value', title: 'Enumeration values' },
        { key: 'constant', title: 'Constants' },
        { key: 'property', title: 'Properties' },
        { key: 'field', title: 'Public fields' },
        { key: 'static-field', title: 'Static fields' },
        { key: 'constructor', title: 'Constructors' },
        { key: 'method', title: 'Public methods' },
        { key: 'static-method', title: 'Static methods' },
        { key: 'operator', title: 'Operators' },
        { key: 'free-function', title: 'Functions' },
        { key: 'macro', title: 'Macros' },
        { key: 'alias', title: 'Type aliases' },
        { key: 'message', title: 'Messages' }
    ];

    /* Alphabetical within a section, with two deliberate exceptions.

       An ENUM's values and a group of CONSTANTS are left in declaration order,
       because for those the order IS the information: SystemStage reads
       kInput 100, kGameplay 200, kMovement 300 ... which is the frame order
       and the whole point of the page, and alphabetising it to kCamera,
       kCollision, kDeferred would throw that away. Same for the Neighbour bit
       constants and for any enum whose last value is a Count sentinel.

       A leading ~ is ignored, so a destructor sits beside its constructors
       rather than at the end of the alphabet. The sort is stable, so
       overloads of one name keep the order the header declares them in. */
    var KEEP_DECLARED = { 'enum-value': 1, 'constant': 1 };

    function byMemberName(a, b) {
        var an = String(a.name), bn = String(b.name);
        /* also drop a trailing "(copy)" / "(UISpace)" - those annotations tell
           overloads apart in the heading and should not move them away from
           the name they overload */
        var x = an.replace(/^~/, '').replace(/\s*\(.*\)\s*$/, '').toLowerCase();
        var y = bn.replace(/^~/, '').replace(/\s*\(.*\)\s*$/, '').toLowerCase();
        if (x !== y) return x < y ? -1 : 1;
        /* same name once the ~ is off: constructors before the destructor */
        var ad = an.charAt(0) === '~' ? 1 : 0;
        var bd = bn.charAt(0) === '~' ? 1 : 0;
        return ad - bd;
    }

    function groupMembers(members) {
        var by = Object.create(null), extra = [];
        (members || []).forEach(function (m) {
            var k = m.kind;
            if (!SECTIONS.some(function (s) { return s.key === k; })) { extra.push(m); return; }
            (by[k] = by[k] || []).push(m);
        });
        if (extra.length) by['method'] = (by['method'] || []).concat(extra);
        return SECTIONS.map(function (s) {
            var items = by[s.key] || [];
            if (!KEEP_DECLARED[s.key]) items = items.slice().sort(byMemberName);
            return { title: s.title, key: s.key, items: items };
        }).filter(function (g) { return g.items.length; });
    }

    /* ------------------------------------------------------- inherited members */

    function inheritedChain(t) {
        var chain = [], seen = {}, cur = t;
        while (cur && cur.inherits && BYNAME[cur.inherits] && !seen[cur.inherits]) {
            seen[cur.inherits] = 1;
            cur = BYNAME[cur.inherits];
            chain.push(cur);
        }
        return chain;
    }

    /* ------------------------------------------------------------ type page */

    function memberTableHtml(typeName, items) {
        var rows = items.map(function (m) {
            return '<tr><td class="n"><a href="#' + attr(typeName) + '-' + attr(m.slug) + '">' +
                esc(m.name) + '</a></td><td class="d">' + inline(m.summary) + '</td></tr>';
        }).join('');
        return '<div class="tablewrap"><table class="members"><tbody>' + rows + '</tbody></table></div>';
    }

    function memberDetailHtml(t, m) {
        var h = [];
        h.push('<div class="member" id="' + attr(t.slug) + '-' + attr(m.slug) + '">');
        h.push('<h3><span class="owner">' + esc(t.name) + '.</span>' + esc(m.name) +
            '<a class="anchor" href="#' + attr(t.slug) + '-' + attr(m.slug) + '" aria-label="Link to this member">#</a></h3>');

        var kindline = [];
        kindline.push(KIND_LABEL[m.kind] || m.kind);
        if (m.access === 'protected') kindline.push('protected');
        /* a lifecycle message is declared nowhere - it is detected by a concept
           in ScriptHooks.h - so it carries its own header */
        var src = m.header || t.header;
        if (m.line) {
            kindline.push(sourceRef(src, m.line, src.split('/').pop() + ':' + m.line));
        }
        h.push('<p class="kindline">' + kindline.join(' &middot; ') + '</p>');

        h.push('<p class="minihead">Declaration</p>');
        h.push(codeBlock(m.signature, 'decl'));

        if (m.params && m.params.length) {
            h.push('<p class="minihead">Parameters</p>');
            h.push('<table class="params"><tbody>' + m.params.map(function (p) {
                return '<tr><td class="p">' + esc(p.name) + '</td><td class="pd">' +
                    (p.type ? '<code>' + hl(p.type) + '</code> &mdash; ' : '') + inline(p.description) + '</td></tr>';
            }).join('') + '</tbody></table>');
        }

        if (m.returns) {
            h.push('<p class="minihead">Returns</p><div class="prose">' + md(m.returns) + '</div>');
        }

        var body = m.description || m.summary;
        if (body) {
            h.push('<p class="minihead">Description</p><div class="prose">' + md(body) + '</div>');
        }

        if (m.unity) {
            h.push('<p class="minihead">In Unity</p><div class="prose"><p><code>' + esc(m.unity) + '</code></p></div>');
        }

        if (m.example) {
            h.push('<p class="minihead">Example</p>');
            h.push(codeBlock(m.example));
        }

        h.push('</div>');
        return h.join('');
    }

    var KIND_LABEL = {
        'constructor': 'Constructor',
        'method': 'Method',
        'static-method': 'Static method',
        'property': 'Property',
        'field': 'Field',
        'static-field': 'Static field',
        'operator': 'Operator',
        'enum-value': 'Enumeration value',
        'alias': 'Type alias',
        'message': 'Message',
        'free-function': 'Function',
        'macro': 'Macro',
        'constant': 'Constant'
    };

    /* The header and line a thing is declared at. With `sourceBase` set in the
       data this becomes a link into a source browser; without one it is plain
       text, which is still the useful part - it is where to look. */
    function sourceRef(header, line, text) {
        var label = esc(text || (header + (line ? ':' + line : '')));
        if (!API.sourceBase) return '<code>' + label + '</code>';
        return '<a href="' + attr(API.sourceBase + header + (line ? '#L' + line : '')) +
            '"><code>' + label + '</code></a>';
    }

    function typePage(t, memberSlug) {
        var mod = MODULES.filter(function (m) { return (m.types || []).indexOf(t.slug) >= 0; })[0];
        var groups = groupMembers(t.members);
        var chain = inheritedChain(t);
        var h = [];

        h.push('<div class="crumb">');
        h.push('<a href="#">Scripting API</a>');
        if (mod) h.push(' / <a href="#module-' + attr(mod.key) + '">' + esc(mod.title) + '</a>');
        h.push(' / ' + esc(t.name));
        h.push('</div>');

        h.push('<div class="titleblock">');
        h.push('<h1>' + esc(t.name) + '</h1>');
        h.push('<p class="declared">' + esc(t.kind) + ' in <code>' + esc(t.ns || 'eng') + '</code>');
        if (t.inherits) {
            h.push(' &nbsp;/&nbsp; inherits from ' + (byName(t.inherits)
                ? '<a href="#' + attr(byName(t.inherits).slug) + '"><code>' + esc(t.inherits) + '</code></a>'
                : '<code>' + esc(t.inherits) + '</code>'));
        }
        h.push('</p>');

        h.push('<div class="chips">');
        h.push('<span class="chip ' + attr(t.audience) + '"><span class="dot ' + attr(t.audience) +
            '"></span>' + esc(AUD_LABEL[t.audience] || t.audience) + '</span>');
        h.push('<span class="chip plain src">' + sourceRef(t.header, t.line) + '</span>');
        if (t.unity) h.push('<span class="chip unity">Unity: <code>' + esc(t.unity) + '</code></span>');
        h.push('</div>');
        h.push('</div>');

        h.push('<div class="prose">');
        h.push(md(t.description || t.summary));
        h.push('</div>');

        if (t.remarks) {
            h.push('<div class="note"><span class="lbl">Worth knowing</span><div class="prose">' +
                md(t.remarks) + '</div></div>');
        }

        if (t.audience === 'host') {
            h.push('<div class="note caution"><span class="lbl">Not for game code</span><p>This is part of the ' +
                'host side of the engine: the program that creates the window and drives the frame. A game script ' +
                'does not touch it. It is documented because <code>&lt;engine/Engine.h&gt;</code> makes it ' +
                'visible.</p></div>');
        }

        groups.forEach(function (g) {
            h.push('<h2 class="sec" id="sec-' + attr(g.key) + '">' + esc(g.title) + '</h2>');
            h.push(memberTableHtml(t.slug, g.items));
        });

        if (chain.length) {
            h.push('<h2 class="sec">Inherited members</h2>');
            chain.forEach(function (base) {
                var pub = (base.members || []).filter(function (m) {
                    return m.kind !== 'constructor' && m.access !== 'protected';
                }).sort(byMemberName);
                if (!pub.length) return;
                h.push('<p class="minihead">From <a href="#' + attr(base.slug) + '">' + esc(base.name) + '</a></p>');
                h.push('<div class="tablewrap"><table class="members"><tbody>' + pub.map(function (m) {
                    return '<tr><td class="n"><a href="#' + attr(base.slug) + '-' + attr(m.slug) + '">' +
                        esc(m.name) + '</a></td><td class="d">' + inline(m.summary) + '</td></tr>';
                }).join('') + '</tbody></table></div>');
            });
        }

        if (t.examples && t.examples.length) {
            h.push('<h2 class="sec" id="sec-examples">Examples</h2>');
            t.examples.forEach(function (ex, i) {
                h.push('<div class="member" id="' + attr(t.slug) + '-example-' + (i + 1) + '">');
                h.push('<h3>' + esc(ex.title) + '</h3>');
                h.push('<p class="kindline">' + (ex.complete ? 'A complete script file' : 'Fragment') + '</p>');
                if (ex.description) h.push('<div class="prose">' + md(ex.description) + '</div>');
                h.push(codeBlock(ex.code));
                h.push('</div>');
            });
        }

        h.push('<h2 class="sec" id="sec-detail">Member details</h2>');
        groups.forEach(function (g) {
            g.items.forEach(function (m) { h.push(memberDetailHtml(t, m)); });
        });

        h.push('<div class="pagefoot">Generated from <code>' + esc(t.header) +
            '</code>. The header carries a longer commentary than this page does, and is the thing to read ' +
            'when you want to know why something works the way it does.</div>');

        /* the jump list */
        var jl = ['<h2>On this page</h2><ul>'];
        groups.forEach(function (g) {
            jl.push('<li class="grp">' + esc(g.title) + '</li>');
            g.items.forEach(function (m) {
                jl.push('<li><a href="#' + attr(t.slug) + '-' + attr(m.slug) + '">' + esc(m.name) + '</a></li>');
            });
        });
        if (t.examples && t.examples.length) {
            jl.push('<li class="grp">Examples</li>');
            t.examples.forEach(function (ex, i) {
                jl.push('<li><a href="#' + attr(t.slug) + '-example-' + (i + 1) + '">' + esc(ex.title) + '</a></li>');
            });
        }
        jl.push('</ul>');

        return { article: h.join('\n'), jump: jl.join(''), scrollTo: memberSlug ? t.name + '-' + memberSlug : null };
    }

    /* ---------------------------------------------------------- module page */

    function modulePage(mod) {
        var names = (mod.types || []).filter(function (n) { return TYPES[n]; });
        var h = [];
        h.push('<div class="crumb"><a href="#">Scripting API</a> / ' + esc(mod.title) + '</div>');
        h.push('<div class="titleblock"><h1 style="font-family:var(--font-sans)">' + esc(mod.title) + '</h1>');
        h.push('<p class="declared">' + names.length + ' types &nbsp;/&nbsp; <code>' + esc(mod.headers || '') + '</code></p></div>');
        h.push('<div class="prose">' + md(mod.blurb || '') + '</div>');

        ['script', 'system', 'host'].forEach(function (aud) {
            var sub = names.filter(function (n) { return TYPES[n].audience === aud; });
            if (!sub.length) return;
            h.push('<h2 class="sec">' + esc(AUD_LABEL[aud]) + '</h2>');
            h.push('<div class="cardgrid">');
            sub.forEach(function (n) {
                var t = TYPES[n];
                h.push('<a class="card" href="#' + attr(n) + '"><span class="t" style="font-family:var(--font-mono)">' +
                    esc(t.name) + '</span><span class="s">' + esc(t.summary) + '</span><span class="n">' +
                    esc(t.header) + '</span></a>');
            });
            h.push('</div>');
        });
        return { article: h.join('\n'), jump: '' };
    }

    /* ------------------------------------------------------------ A-Z index */

    function indexPage() {
        var letters = {};
        ORDER.slice().sort(bySlugName)
            .forEach(function (n) {
                var L = TYPES[n].name[0].toUpperCase();
                (letters[L] = letters[L] || []).push(n);
            });
        var keys = Object.keys(letters).sort();

        var h = [];
        h.push('<div class="crumb"><a href="#">Scripting API</a> / All classes</div>');
        h.push('<div class="titleblock"><h1 style="font-family:var(--font-sans)">All classes, A to Z</h1>');
        h.push('<p class="declared">Every public type reachable from <code>&lt;engine/Engine.h&gt;</code> &mdash; ' +
            ORDER.length + ' of them.</p></div>');

        h.push('<div class="azbar">');
        'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').forEach(function (L) {
            h.push(letters[L] ? '<a href="#index-' + L + '">' + L + '</a>' : '<span>' + L + '</span>');
        });
        h.push('</div>');

        keys.forEach(function (L) {
            h.push('<section class="azgroup" id="index-' + L + '"><h2>' + L + '</h2>');
            h.push('<div class="tablewrap"><table class="members"><tbody>');
            letters[L].forEach(function (n) {
                var t = TYPES[n];
                h.push('<tr><td class="n"><span class="dot ' + attr(t.audience) +
                    '" style="display:inline-block;margin-right:7px"></span><a href="#' + attr(n) + '">' +
                    esc(t.name) + '</a></td><td class="d">' + inline(t.summary) + '</td></tr>');
            });
            h.push('</tbody></table></div></section>');
        });

        var jl = ['<h2>Letters</h2><ul>'];
        keys.forEach(function (L) { jl.push('<li><a href="#index-' + L + '">' + L + '</a></li>'); });
        jl.push('</ul>');

        return { article: h.join('\n'), jump: jl.join('') };
    }

    /* ----------------------------------------------------------- guide page */

    function guidePage(slug) {
        var g = (API.guides || []).filter(function (x) { return x.slug === slug; })[0];
        var tpl = document.getElementById('guide-' + slug);
        if (!g || !tpl) return null;

        var h = [];
        h.push('<div class="crumb"><a href="#">Scripting API</a> / Guides / ' + esc(g.title) + '</div>');
        h.push('<div class="titleblock"><h1 style="font-family:var(--font-sans)">' + esc(g.title) + '</h1>');
        if (g.lede) h.push('<p class="declared">' + esc(g.lede) + '</p>');
        h.push('</div>');
        h.push('<div class="prose guidebody">' + tpl.innerHTML + '</div>');
        return { article: h.join('\n'), jump: '', isGuide: true };
    }

    /* ------------------------------------------------------------ home page */

    function homePage() {
        var counts = { script: 0, system: 0, host: 0 }, members = 0, examples = 0;
        ORDER.forEach(function (n) {
            var t = TYPES[n];
            counts[t.audience] = (counts[t.audience] || 0) + 1;
            members += (t.members || []).length;
            examples += (t.examples || []).length;
        });

        var h = [];
        h.push('<div class="hero">');
        h.push('<p class="eyebrow">Engine2D &middot; Scripting API</p>');
        h.push('<h1>Everything a script can reach</h1>');
        h.push('<p class="lede">One include gives a script the whole engine. This is what is behind it: ' +
            'every public type, every member, with the signature copied from the header it is declared in.</p>');
        h.push('</div>');

        h.push(codeBlock('#include <engine/Engine.h>\nusing namespace eng;\n\nnamespace {\n\nclass Hello final : public MyScriptBehaviour {\npublic:\n    float speed = 120.0f;   // shows up in the Inspector\n\n    void OnStart() {\n        Debug::Log("Hello from {}", name);\n    }\n\n    void OnFixedUpdate() {\n        transform->Translate(Vec2{speed * Time::FixedDeltaTime(), 0.0f});\n    }\n};\n\n}   // namespace\n\nENGINE_REGISTER_SCRIPT(Hello)'));

        h.push('<div class="statrow">');
        h.push('<div><span class="v">' + ORDER.length + '</span><span class="k">Types</span></div>');
        h.push('<div><span class="v">' + members + '</span><span class="k">Members</span></div>');
        h.push('<div><span class="v">' + (API.headerCount || 58) + '</span><span class="k">Headers</span></div>');
        h.push('<div><span class="v">' + examples + '</span><span class="k">Examples</span></div>');
        h.push('</div>');

        h.push('<h2 class="sec">Start here</h2>');
        h.push('<div class="cardgrid">');
        (API.guides || []).forEach(function (g) {
            h.push('<a class="card" href="#guide-' + attr(g.slug) + '"><span class="t">' + esc(g.title) +
                '</span><span class="s">' + esc(g.lede || '') + '</span></a>');
        });
        h.push('</div>');

        h.push('<h2 class="sec">The reference</h2>');
        h.push('<div class="cardgrid">');
        MODULES.forEach(function (mod) {
            var names = (mod.types || []).filter(function (n) { return TYPES[n]; });
            if (!names.length) return;
            h.push('<a class="card" href="#module-' + attr(mod.key) + '"><span class="t">' + esc(mod.title) +
                '</span><span class="s">' + esc(mod.tagline || '') +
                '</span><span class="n">' + names.length + ' types</span></a>');
        });
        h.push('</div>');

        h.push('<h2 class="sec">How a page is marked</h2>');
        h.push('<div class="prose"><p>Not everything reachable from <code>&lt;engine/Engine.h&gt;</code> is meant ' +
            'for a script. Every page says which it is, and the dot in the sidebar repeats it.</p>' +
            '<table><thead><tr><th></th><th>Means</th><th>Count</th></tr></thead><tbody>' +
            '<tr><td><span class="chip script"><span class="dot script"></span>Scripting</span></td>' +
            '<td>A game script calls this directly.</td><td>' + counts.script + '</td></tr>' +
            '<tr><td><span class="chip system"><span class="dot system"></span>Engine system</span></td>' +
            '<td>Runs itself. Read the page to understand the frame order, or to find the component.</td><td>' + counts.system + '</td></tr>' +
            '<tr><td><span class="chip host"><span class="dot host"></span>Host only</span></td>' +
            '<td>The program that creates the window. A script never touches it.</td><td>' + counts.host + '</td></tr>' +
            '</tbody></table></div>');

        h.push('<div class="pagefoot">Built from the headers under <code>engine/include/engine/</code>. ' +
            'Every signature on every page is the declaration as it is written there, and every page links ' +
            'back to the file and line.</div>');

        return { article: h.join('\n'), jump: '' };
    }

    /* ------------------------------------------------------------- rendering */

    var elArticle, elJump, elRail, elSearch, elResults;

    function parseHash() {
        var raw = (location.hash || '').replace(/^#/, '');
        /* a link written by hand, or an older link to a page whose name had a
           space in it, arrives percent-encoded */
        try { raw = decodeURIComponent(raw); } catch (e) { /* leave it as it came */ }
        if (!raw) return { route: 'home' };
        if (raw === 'index') return { route: 'index' };
        if (/^index-[A-Z]$/.test(raw)) return { route: 'index', anchor: raw };
        var seg = raw.split('-');
        if (seg[0] === 'guide' && seg.length > 1) return { route: 'guide', slug: seg.slice(1).join('-') };
        if (seg[0] === 'module' && seg.length > 1) return { route: 'module', key: seg.slice(1).join('-') };
        if (TYPES[raw]) return { route: 'type', type: raw };

        /* LONGEST type-slug prefix wins. A grouping page's slug has hyphens in
           it - "Vec2-operators" - so a plain first-segment split would send
           #Vec2-operators-operator-add to Vec2 with a nonsense member. */
        for (var k = seg.length - 1; k > 0; k--) {
            var cand = seg.slice(0, k).join('-');
            if (TYPES[cand]) {
                return { route: 'type', type: cand, member: seg.slice(k).join('-') };
            }
        }
        return { route: 'missing', raw: raw };
    }

    function missingPage(raw) {
        var h = [];
        h.push('<div class="crumb"><a href="#">Scripting API</a> / Not found</div>');
        h.push('<div class="titleblock"><h1 style="font-family:var(--font-sans)">No page called that</h1>');
        h.push('<p class="declared"><code>' + esc(raw) + '</code> is not a type in this reference.</p></div>');
        var near = SEARCH.filter(function (s) { return s.lower.indexOf(raw.toLowerCase().slice(0, 4)) >= 0; }).slice(0, 12);
        h.push('<div class="prose"><p>Try the <a href="#index">A to Z index</a>, or the search box above.</p>');
        if (near.length) {
            h.push('<p>Closest matches:</p><ul>');
            near.forEach(function (s) {
                h.push('<li><a href="' + attr(s.href) + '"><code>' + esc(s.owner && s.owner !== 'guide' ? s.owner + '.' + s.name : s.name) + '</code></a></li>');
            });
            h.push('</ul>');
        }
        h.push('</div>');
        return { article: h.join('\n'), jump: '' };
    }

    function render() {
        var r = parseHash();
        var page, active = '';

        if (r.route === 'home') { page = homePage(); }
        else if (r.route === 'index') { page = indexPage(); active = 'index'; }
        else if (r.route === 'guide') {
            page = guidePage(r.slug);
            active = 'guide-' + r.slug;
            if (!page) page = missingPage('guide-' + r.slug);
        } else if (r.route === 'module') {
            var mod = MODULES.filter(function (m) { return m.key === r.key; })[0];
            page = mod ? modulePage(mod) : missingPage('module-' + r.key);
            active = 'module-' + r.key;
        } else if (r.route === 'type') {
            page = typePage(TYPES[r.type], r.member);
            active = r.type;
        } else {
            page = missingPage(r.raw);
        }

        elArticle.innerHTML = page.article;
        elJump.innerHTML = page.jump || '';
        elJump.hidden = !page.jump;
        elRail.innerHTML = railHtml(active);

        document.title = pageTitle(r);

        var target = null;
        if (r.route === 'type' && r.member) target = document.getElementById(r.type + '-' + r.member);
        else if (r.anchor) target = document.getElementById(r.anchor);
        if (target) target.scrollIntoView();
        else window.scrollTo(0, 0);

        var on = elRail.querySelector('a.on');
        if (on && on.scrollIntoView) {
            var rb = on.getBoundingClientRect();
            if (rb.top < 60 || rb.bottom > window.innerHeight - 20) on.scrollIntoView({ block: 'center' });
        }

        document.body.classList.remove('rail-open');
        markJump();
    }

    function pageTitle(r) {
        var base = 'Engine2D Scripting API';
        if (r.route === 'type') return (TYPES[r.type] ? TYPES[r.type].name : r.type) + ' - ' + base;
        if (r.route === 'guide') {
            var g = (API.guides || []).filter(function (x) { return x.slug === r.slug; })[0];
            return (g ? g.title : 'Guide') + ' - ' + base;
        }
        if (r.route === 'module') {
            var m = MODULES.filter(function (x) { return x.key === r.key; })[0];
            return (m ? m.title : 'Module') + ' - ' + base;
        }
        if (r.route === 'index') return 'All classes - ' + base;
        return base;
    }

    /* the jump list follows the scroll */
    var jumpTargets = [];
    function markJump() {
        jumpTargets = [].slice.call(elJump.querySelectorAll('a')).map(function (a) {
            return { a: a, el: document.getElementById(a.getAttribute('href').slice(1)) };
        }).filter(function (x) { return x.el; });
        onScroll();
    }
    var scrollQueued = false;
    function onScroll() {
        if (scrollQueued) return;
        scrollQueued = true;
        window.requestAnimationFrame(function () {
            scrollQueued = false;
            var best = null, bestTop = -Infinity;
            jumpTargets.forEach(function (x) {
                var top = x.el.getBoundingClientRect().top - 120;
                if (top <= 0 && top > bestTop) { bestTop = top; best = x; }
            });
            jumpTargets.forEach(function (x) { x.a.classList.toggle('on', x === best); });
        });
    }

    /* ----------------------------------------------------------- the search */

    function score(entry, q) {
        var i = entry.lower.indexOf(q);
        if (i < 0) return -1;
        var s = 100 - i * 4 - entry.weight * 3;
        if (i === 0) s += 40;
        if (entry.lower === q) s += 80;
        s -= Math.min(20, entry.name.length / 3);
        return s;
    }

    function runSearch(q) {
        q = q.trim().toLowerCase();
        if (!q) { elResults.hidden = true; elResults.innerHTML = ''; return; }

        /* "Entity.Find" and "Entity Find" both mean a member of a type */
        var own = null, mem = q;
        var dot = q.indexOf('.');
        if (dot > 0) { own = q.slice(0, dot); mem = q.slice(dot + 1); }

        var hits = [];
        SEARCH.forEach(function (e) {
            if (own !== null) {
                if (e.owner.toLowerCase().indexOf(own) !== 0) return;
                if (mem && e.lower.indexOf(mem) < 0) return;
                hits.push({ e: e, s: 120 - e.lower.indexOf(mem) * 3 });
                return;
            }
            var s = score(e, q);
            if (s >= 0) hits.push({ e: e, s: s });
        });

        hits.sort(function (a, b) { return b.s - a.s; });
        hits = hits.slice(0, 40);

        if (!hits.length) {
            elResults.innerHTML = '<p class="r-empty">Nothing matches that.</p>';
            elResults.hidden = false;
            return;
        }

        var needle = own !== null ? mem : q;
        elResults.innerHTML = '<ul>' + hits.map(function (h, i) {
            var e = h.e;
            var nm = esc(e.name);
            if (needle) {
                var at = e.lower.indexOf(needle);
                if (at >= 0) {
                    nm = esc(e.name.slice(0, at)) + '<mark>' + esc(e.name.slice(at, at + needle.length)) +
                        '</mark>' + esc(e.name.slice(at + needle.length));
                }
            }
            return '<li><a href="' + attr(e.href) + '"' + (i === 0 ? ' class="on"' : '') + '>' +
                (e.owner && e.owner !== 'guide' ? '<span class="r-own">' + esc(e.owner) + '.</span>' : '') +
                '<span class="r-name">' + nm + '</span>' +
                '<span class="r-kind">' + esc(KIND_LABEL[e.kind] || e.kind) + '</span></a></li>';
        }).join('') + '</ul>';
        elResults.hidden = false;
    }

    function moveSel(dir) {
        var links = [].slice.call(elResults.querySelectorAll('a'));
        if (!links.length) return;
        var at = links.findIndex(function (a) { return a.classList.contains('on'); });
        links.forEach(function (a) { a.classList.remove('on'); });
        var next = Math.max(0, Math.min(links.length - 1, (at < 0 ? 0 : at) + dir));
        links[next].classList.add('on');
        links[next].scrollIntoView({ block: 'nearest' });
    }

    /* ------------------------------------------------------------------ boot */

    function boot() {
        elArticle = document.getElementById('article');
        elJump = document.getElementById('jump');
        elRail = document.getElementById('rail');
        elSearch = document.getElementById('search');
        elResults = document.getElementById('results');

        if (!API) {
            elArticle.innerHTML = '<div class="nojs"><h1>The reference data did not load</h1>' +
                '<p>This page needs <code>api-data.js</code> beside it.</p></div>';
            return;
        }

        buildModel();

        window.addEventListener('hashchange', render);
        window.addEventListener('scroll', onScroll, { passive: true });

        elSearch.addEventListener('input', function () { runSearch(elSearch.value); });
        elSearch.addEventListener('keydown', function (ev) {
            if (ev.key === 'ArrowDown') { ev.preventDefault(); moveSel(1); }
            else if (ev.key === 'ArrowUp') { ev.preventDefault(); moveSel(-1); }
            else if (ev.key === 'Enter') {
                var sel = elResults.querySelector('a.on') || elResults.querySelector('a');
                if (sel) { ev.preventDefault(); location.hash = sel.getAttribute('href'); elSearch.blur(); }
            } else if (ev.key === 'Escape') {
                elSearch.value = '';
                elResults.hidden = true;
                elSearch.blur();
            }
        });
        elResults.addEventListener('click', function () {
            elSearch.value = '';
            elResults.hidden = true;
        });
        document.addEventListener('click', function (ev) {
            if (!elResults.hidden && !ev.target.closest('.searchwrap')) elResults.hidden = true;
        });
        document.addEventListener('keydown', function (ev) {
            if (ev.key === '/' && document.activeElement !== elSearch &&
                !/^(INPUT|TEXTAREA)$/.test(document.activeElement.tagName)) {
                ev.preventDefault();
                elSearch.focus();
            }
        });

        document.getElementById('railtoggle').addEventListener('click', function () {
            document.body.classList.toggle('rail-open');
        });
        document.querySelector('.scrim').addEventListener('click', function () {
            document.body.classList.remove('rail-open');
        });

        var tb = document.getElementById('themetoggle');
        var stored = null;
        try { stored = localStorage.getItem('engine-api-theme'); } catch (e) { /* private window */ }
        if (stored === 'dark' || stored === 'light') document.documentElement.setAttribute('data-theme', stored);
        tb.addEventListener('click', function () {
            var now = document.documentElement.getAttribute('data-theme');
            var dark = now === 'dark' ||
                (!now && window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
            var next = dark ? 'light' : 'dark';
            document.documentElement.setAttribute('data-theme', next);
            try { localStorage.setItem('engine-api-theme', next); } catch (e) { /* ignore */ }
        });

        render();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
    else boot();
})();
