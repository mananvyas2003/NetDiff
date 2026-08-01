// ============================================================
//  web_dashboard.cpp
//  Embedded HTTP server (Winsock2) for the KiCad parser.
//  Serves:
//      GET /            -> the dashboard HTML page
//      GET /api/data    -> JSON snapshot of the parse result
//      GET /favicon.ico -> 204 (so the browser stops asking)
//  Links Ws2_32 and Shell32 via #pragma, so no project /
//  linker changes are required: just add this .cpp + the .h
//  to the Visual Studio project.
// ============================================================

#include "web_dashboard.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <cstdio>
#include <cstdint>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Shell32.lib")

// ------------------------------------------------------------
//  Internal helpers (file-local)
// ------------------------------------------------------------
namespace
{
    // ----- JSON string escaping ------------------------------
    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char ch : s)
        {
            unsigned char c = static_cast<unsigned char>(ch);
            switch (c)
            {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", c);
                    out += hex;
                }
                else
                {
                    out += ch;
                }
            }
        }
        return out;
    }

    // ----- Build the JSON payload from the parse result ------
    std::string BuildJson(const WebDashboardData& d)
    {
        std::ostringstream o;

        o << "{\n";

        // ---- file ----
        o << "  \"file\": {\n";
        o << "    \"path\": \"" << JsonEscape(d.filepath) << "\",\n";
        o << "    \"size_bytes\": " << d.file_size_bytes << ",\n";
        o << "    \"parse_time_ms\": " << d.parse_time_ms << "\n";
        o << "  },\n";

        // ---- parser ----
        o << "  \"parser\": {\n";
        o << "    \"ast_nodes\": " << d.ast_node_count << ",\n";
        o << "    \"root_node\": " << d.root_node_idx << ",\n";
        o << "    \"breakdown\": {\n";
        o << "      \"list\": " << d.parser_stats.list_nodes << ",\n";
        o << "      \"symbol\": " << d.parser_stats.symbol_nodes << ",\n";
        o << "      \"number\": " << d.parser_stats.number_nodes << ",\n";
        o << "      \"string\": " << d.parser_stats.string_nodes << "\n";
        o << "    }\n";
        o << "  },\n";

        // ---- interpreter ----
        const Schematic* s = d.schematic;
        const std::size_t wc = s ? s->wires.size() : 0;
        const std::size_t jc = s ? s->junctions.size() : 0;
        const std::size_t lc = s ? s->labels.size() : 0;
        const std::size_t cc = s ? s->components.size() : 0;

        o << "  \"interpreter\": {\n";
        o << "    \"wire_count\": " << wc << ",\n";
        o << "    \"junction_count\": " << jc << ",\n";
        o << "    \"label_count\": " << lc << ",\n";
        o << "    \"component_count\": " << cc << ",\n";

        // wires
        o << "    \"wires\": [";
        for (std::size_t i = 0; i < wc; ++i)
        {
            const WireSegment& w = s->wires[i];
            o << (i ? "," : "")
                << "\n      {\"x1\": " << w.start.x << ", \"y1\": " << w.start.y
                << ", \"x2\": " << w.end.x << ", \"y2\": " << w.end.y << "}";
        }
        o << (wc ? "\n    " : "") << "],\n";

        // junctions
        o << "    \"junctions\": [";
        for (std::size_t i = 0; i < jc; ++i)
        {
            const Junction& j = s->junctions[i];
            o << (i ? "," : "")
                << "\n      {\"x\": " << j.location.x << ", \"y\": " << j.location.y << "}";
        }
        o << (jc ? "\n    " : "") << "],\n";

        // labels
        o << "    \"labels\": [";
        for (std::size_t i = 0; i < lc; ++i)
        {
            const NetLabel& l = s->labels[i];
            o << (i ? "," : "")
                << "\n      {\"name\": \"" << JsonEscape(l.name)
                << "\", \"x\": " << l.location.x << ", \"y\": " << l.location.y << "}";
        }
        o << (lc ? "\n    " : "") << "],\n";

        // components
        o << "    \"components\": [";
        for (std::size_t i = 0; i < cc; ++i)
        {
            const Component& c = s->components[i];
            o << (i ? "," : "")
                << "\n      {\"reference\": \"" << JsonEscape(c.reference)
                << "\", \"value\": \"" << JsonEscape(c.value)
                << "\", \"x\": " << c.location.x << ", \"y\": " << c.location.y
                << ", \"rotation\": " << c.rotation << "}";
        }
        o << (cc ? "\n    " : "") << "]\n";
        o << "  },\n";

        // ---- net resolver ----
        const std::vector<ConnectivityNode>* nodes = d.nodes;
        const std::vector<Edge>* edges = d.edges;
        const std::size_t nc = nodes ? nodes->size() : 0;
        const std::size_t ec = edges ? edges->size() : 0;

        o << "  \"net\": {\n";
        o << "    \"node_count\": " << nc << ",\n";
        o << "    \"edge_count\": " << ec << ",\n";

        o << "    \"nodes\": [";
        for (std::size_t i = 0; i < nc; ++i)
        {
            const ConnectivityNode& n = (*nodes)[i];
            o << (i ? "," : "")
                << "\n      {\"id\": " << n.id
                << ", \"x\": " << n.position.x << ", \"y\": " << n.position.y << "}";
        }
        o << (nc ? "\n    " : "") << "],\n";

        o << "    \"edges\": [";
        for (std::size_t i = 0; i < ec; ++i)
        {
            const Edge& e = (*edges)[i];
            o << (i ? "," : "")
                << "\n      {\"id\": " << i
                << ", \"start\": " << e.start_node << ", \"end\": " << e.end_node << "}";
        }
        o << (ec ? "\n    " : "") << "]\n";
        o << "  }\n";

        o << "}\n";
        return o.str();
    }

    // ----- The dashboard page (HTML + CSS + JS) --------------
    std::string BuildHtml()
    {
        // Chunking the raw string literal into an aggregate block to bypass MSVC's 64KB C2026 token limit.
        std::string html_content = "";

        html_content += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>KiCad Parser // Dashboard</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
<style>
  :root{
    --bg:#070b0a;
    --panel:#0d1413;
    --panel-2:#101a18;
    --line:#1c2b27;
    --line-soft:#16221f;
    --ink:#dbe7e2;
    --muted:#728079;
    --muted-2:#4d5b55;
    --mint:#5be3a6;
    --mint-dim:#2f7f5e;
    --cyan:#54c7f0;
    --amber:#ffb454;
    --rose:#ff6b6b;
    --violet:#a78bfa;
    --mono:'JetBrains Mono',ui-monospace,'SF Mono',Menlo,Consolas,monospace;
    --sans:'IBM Plex Sans',system-ui,-apple-system,'Segoe UI',sans-serif;
  }
  *{box-sizing:border-box}
  html,body{margin:0;padding:0}
  body{
    background:
      radial-gradient(1200px 600px at 80% -10%, rgba(91,227,166,.06), transparent 60%),
      radial-gradient(900px 500px at -10% 10%, rgba(84,199,240,.05), transparent 55%),
      var(--bg);
    color:var(--ink);
    font-family:var(--sans);
    font-size:14px;
    line-height:1.5;
    -webkit-font-smoothing:antialiased;
    min-height:100vh;
  }
  a{color:inherit}
  .wrap{max-width:1320px;margin:0 auto;padding:28px 24px 64px}

  /* ---------- header ---------- */
  header{
    display:flex;align-items:flex-end;justify-content:space-between;
    gap:24px;flex-wrap:wrap;
    padding-bottom:20px;margin-bottom:24px;
    border-bottom:1px solid var(--line);
    animation:rise .5s ease both;
  }
  .brand{display:flex;flex-direction:column;gap:6px}
  .brand .kick{
    font-family:var(--mono);font-size:11px;letter-spacing:.32em;
    color:var(--mint);text-transform:uppercase;
  }
  .brand h1{
    margin:0;font-family:var(--mono);font-weight:700;
    font-size:26px;letter-spacing:-.01em;color:var(--ink);
  }
  .brand h1 span{color:var(--muted-2)}
  .filemeta{
    font-family:var(--mono);font-size:12px;color:var(--muted);
    display:flex;gap:14px;flex-wrap:wrap;margin-top:2px;
  }
  .filemeta b{color:var(--ink);font-weight:500}
  .status{
    display:flex;align-items:center;gap:9px;
    font-family:var(--mono);font-size:12px;letter-spacing:.12em;
    text-transform:uppercase;color:var(--mint);
    border:1px solid var(--mint-dim);border-radius:999px;
    padding:7px 14px;background:rgba(91,227,166,.05);
    white-space:nowrap;
  }
  .dot{width:8px;height:8px;border-radius:50%;background:var(--mint);
    box-shadow:0 0 0 0 rgba(91,227,166,.6);animation:pulse 2s infinite}
  @keyframes pulse{
    0%{box-shadow:0 0 0 0 rgba(91,227,166,.5)}
    70%{box-shadow:0 0 0 7px rgba(91,227,166,0)}
    100%{box-shadow:0 0 0 0 rgba(91,227,166,0)}
  }

  /* ---------- kpi cards ---------- */
  .kpis{
    display:grid;gap:12px;margin-bottom:24px;
    grid-template-columns:repeat(auto-fit,minmax(148px,1fr));
  }
  .card{
    background:linear-gradient(180deg,var(--panel-2),var(--panel));
    border:1px solid var(--line);border-radius:12px;
    padding:16px 16px 14px;position:relative;overflow:hidden;
    animation:rise .5s ease both;
  }
  .card::before{
    content:"";position:absolute;inset:0 0 auto 0;height:2px;
    background:linear-gradient(90deg,var(--accent,var(--mint)),transparent 70%);
    opacity:.8;
  }
  .card .lab{
    font-family:var(--mono);font-size:10.5px;letter-spacing:.16em;
    text-transform:uppercase;color:var(--muted);
  }
  .card .val{
    font-family:var(--mono);font-weight:700;font-size:30px;
    line-height:1.05;margin-top:10px;color:var(--ink);
    font-variant-numeric:tabular-nums;
  }
  .card .sub{font-size:11px;color:var(--muted-2);margin-top:4px}
  .card.accent-mint{--accent:var(--mint)} .card.accent-mint .val{color:var(--mint)}
  .card.accent-cyan{--accent:var(--cyan)}
  .card.accent-amber{--accent:var(--amber)}
  .card.accent-violet{--accent:var(--violet)}

  /* ---------- layout ---------- */
  .grid{display:grid;gap:16px;grid-template-columns:1.65fr 1fr}
  @media (max-width:920px){.grid{grid-template-columns:1fr}}

  .panel{
    background:linear-gradient(180deg,var(--panel-2),var(--panel));
    border:1px solid var(--line);border-radius:14px;
    animation:rise .5s ease both;
  }
  .panel-head{
    display:flex;align-items:center;justify-content:space-between;
    gap:12px;padding:14px 16px;border-bottom:1px solid var(--line-soft);
  }
  .panel-head h2{
    margin:0;font-family:var(--mono);font-size:12px;font-weight:600;
    letter-spacing:.18em;text-transform:uppercase;color:var(--ink);
  }
  .panel-head .hint{font-family:var(--mono);font-size:11px;color:var(--muted-2)}
  .panel-body{padding:16px}

  /* ---------- schematic canvas ---------- */
  .toolbar{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
  .tbtn{
    font-family:var(--mono);font-size:11px;letter-spacing:.04em;
    color:var(--muted);background:transparent;cursor:pointer;
    border:1px solid var(--line);border-radius:7px;padding:5px 9px;
    transition:all .15s ease;user-select:none;
  }
  .tbtn:hover{color:var(--ink);border-color:var(--mint-dim)}
  .tbtn.on{color:var(--bg);background:var(--mint);border-color:var(--mint)}
  .canvas-shell{
    position:relative;height:520px;border-radius:10px;overflow:hidden;
    background-color:#060a09;
    background-image:
      linear-gradient(rgba(91,227,166,.045) 1px,transparent 1px),
      linear-gradient(90deg,rgba(91,227,166,.045) 1px,transparent 1px),
      linear-gradient(rgba(91,227,166,.09) 1px,transparent 1px),
      linear-gradient(90deg,rgba(91,227,166,.09) 1px,transparent 1px);
    background-size:14px 14px,14px 14px,70px 70px,70px 70px;
    border:1px solid var(--line-soft);
    cursor:grab;
  }
  .canvas-shell.nogrid{background-image:none}
  .canvas-shell.dragging{cursor:grabbing}
  #schematic{width:100%;height:100%;display:block;touch-action:none}
  .legend{
    display:flex;gap:18px;flex-wrap:wrap;margin-top:12px;
    font-family:var(--mono);font-size:11px;color:var(--muted);
  }
  .legend span{display:inline-flex;align-items:center;gap:7px}
  .swatch{width:11px;height:11px;border-radius:3px;display:inline-block}
  .coord{
    position:absolute;left:10px;bottom:8px;z-index:5;
    font-family:var(--mono);font-size:10.5px;color:var(--muted);
    background:rgba(6,10,9,.7);border:1px solid var(--line-soft);
    border-radius:6px;padding:3px 8px;pointer-events:none;
  }
  .empty{
    position:absolute;inset:0;display:flex;align-items:center;justify-content:center;
    font-family:var(--mono);font-size:12px;color:var(--muted-2);letter-spacing:.1em;
  }
)HTML";

        html_content += R"HTML(
  /* ---------- ast composition ---------- */
  .bar{
    display:flex;height:34px;width:100%;border-radius:8px;overflow:hidden;
    border:1px solid var(--line-soft);background:#06100d;
  }
  .bar i{display:block;height:100%;transition:width .6s cubic-bezier(.2,.7,.2,1)}
  .legrow{display:flex;flex-direction:column;gap:9px;margin-top:16px}
  .leg{display:flex;align-items:center;gap:10px;font-size:13px}
  .leg .k{display:inline-flex;align-items:center;gap:8px;color:var(--ink);min-width:88px}
  .leg .bar-mini{flex:1;height:6px;border-radius:6px;background:#0c1714;overflow:hidden}
  .leg .bar-mini i{display:block;height:100%}
  .leg .n{font-family:var(--mono);color:var(--muted);font-variant-numeric:tabular-nums;min-width:78px;text-align:right}

  /* ---------- connectivity ---------- */
  .net-shell{
    height:200px;border-radius:10px;overflow:hidden;
    background:#060a09;border:1px solid var(--line-soft);position:relative;
  }
  #netgraph{width:100%;height:100%;display:block}

  /* ---------- warnings ---------- */
  .warn{
    display:flex;align-items:flex-start;gap:10px;
    font-family:var(--mono);font-size:12px;padding:10px 12px;
    border-radius:9px;border:1px solid margin-top:10px;line-height:1.45;
  }
  .warn.ok{color:var(--mint);border-color:var(--mint-dim);background:rgba(91,227,166,.05)}
  .warn.bad{color:var(--amber);border-color:#5a4321;background:rgba(255,180,84,.05)}

  /* ---------- tables ---------- */
  .tabs{display:flex;gap:4px;flex-wrap:wrap}
  .tab{
    font-family:var(--mono);font-size:11.5px;letter-spacing:.06em;
    text-transform:uppercase;color:var(--muted);cursor:pointer;
    padding:7px 12px;border-radius:8px 8px 0 0;border:1px solid transparent;
    border-bottom:none;transition:all .15s ease;
  }
  .tab:hover{color:var(--ink)}
  .tab.on{color:var(--mint);background:var(--panel-2);border-color:var(--line-soft)}
  .tab .c{color:var(--muted-2);margin-left:6px}
  .tablewrap{max-height:320px;overflow:auto;border-radius:0 10px 10px 10px;border:1px solid var(--line-soft)}
  table{width:100%;border-collapse:collapse;font-size:13px}
  thead th{
    position:sticky;top:0;background:#0c1512;text-align:left;
    font-family:var(--mono);font-size:10.5px;letter-spacing:.12em;
    text-transform:uppercase;color:var(--muted);font-weight:500;
    padding:10px 14px;border-bottom:1px solid var(--line);z-index:2;
  }
  tbody td{padding:8px 14px;border-bottom:1px solid var(--line-soft);
    font-family:var(--mono);color:var(--ink);font-variant-numeric:tabular-nums}
  tbody tr:hover td{background:rgba(91,227,166,.035)}
  tbody td.idx{color:var(--muted-2)}
  tbody td.tag{color:var(--cyan)}
  .tnote{font-family:var(--mono);font-size:11px;color:var(--muted-2);padding:10px 14px}
  .tnote.center{text-align:center}

  footer{
    margin-top:28px;padding-top:18px;border-top:1px solid var(--line);
    display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;
    font-family:var(--mono);font-size:11px;color:var(--muted-2);
  }

  @keyframes rise{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}
  .d1{animation-delay:.04s}.d2{animation-delay:.08s}.d3{animation-delay:.12s}
  .d4{animation-delay:.16s}.d5{animation-delay:.2s}.d6{animation-delay:.24s}
  .d7{animation-delay:.28s}.d8{animation-delay:.32s}

  ::-webkit-scrollbar{width:10px;height:10px}
  ::-webkit-scrollbar-thumb{background:#1f2e2a;border-radius:8px;border:2px solid var(--panel)}
  ::-webkit-scrollbar-track{background:transparent}
</style>
</head>
<body>
<div class="wrap">

  <header>
    <div class="brand">
      <div class="kick">KiCad Schematic Pipeline</div>
      <h1>Parser <span>//</span> Dashboard</h1>
      <div class="filemeta" id="filemeta"><b>loading…</b></div>
    </div>
    <div class="status"><span class="dot"></span> Execution Complete</div>
  </header>
)HTML";

        html_content += R"HTML(
  <section class="kpis" id="kpis"></section>

  <section class="grid">
    <div class="panel d5">
      <div class="panel-head">
        <h2>Schematic Canvas</h2>
        <div class="toolbar">
          <button class="tbtn" id="btn-fit">Fit</button>
          <button class="tbtn on" id="btn-grid">Grid</button>
          <button class="tbtn on" id="btn-junc">Junctions</button>
          <button class="tbtn on" id="btn-lab">Labels</button>
        </div>
      </div>
      <div class="panel-body">
        <div class="canvas-shell" id="canvas-shell">
          <svg id="schematic" xmlns="http://www.w3.org/2000/svg"></svg>
          <div class="coord" id="coord">x —  y —</div>
          <div class="empty" id="canvas-empty" style="display:none">NO GEOMETRY EXTRACTED</div>
        </div>
        <div class="legend">
          <span><i class="swatch" style="background:var(--mint)"></i> Wire</span>
          <span><i class="swatch" style="background:var(--amber);border-radius:50%"></i> Junction</span>
          <span><i class="swatch" style="background:var(--cyan)"></i> Label</span>
          <span><i class="swatch" style="background:var(--violet)"></i> Component</span>
          <span style="color:var(--muted-2)">scroll = zoom · drag = pan</span>
        </div>
      </div>
    </div>

    <div style="display:flex;flex-direction:column;gap:16px">
      <div class="panel d6">
        <div class="panel-head"><h2>AST Composition</h2><span class="hint" id="ast-total"></span></div>
        <div class="panel-body">
          <div class="bar" id="ast-bar"></div>
          <div class="legrow" id="ast-leg"></div>
        </div>
      </div>

      <div class="panel d7">
        <div class="panel-head"><h2>Connectivity Graph</h2><span class="hint" id="net-hint"></span></div>
        <div class="panel-body">
          <div class="net-shell">
            <svg id="netgraph" xmlns="http://www.w3.org/2000/svg"></svg>
            <div class="empty" id="net-empty" style="display:none">NO NET DATA</div>
          </div>
          <div id="warnbox"></div>
        </div>
      </div>
    </div>
  </section>

  <section class="panel d8" style="margin-top:16px">
    <div class="panel-head">
      <div class="tabs" id="tabs"></div>
      <span class="hint" id="table-count"></span>
    </div>
    <div class="panel-body">
      <div class="tablewrap"><div id="table"></div></div>
    </div>
  </section>

  <footer>
    <span id="foot-left">KiCad parser dashboard</span>
    <span id="foot-right"></span>
  </footer>
</div>
)HTML";

        html_content += R"HTML(
<script>
(function(){
  "use strict";
  var nf = new Intl.NumberFormat('en-US');
  var $ = function(id){ return document.getElementById(id); };
  function el(tag, cls, html){ var e=document.createElement(tag); if(cls)e.className=cls; if(html!=null)e.innerHTML=html; return e; }
  function num(v, dp){ if(v==null||isNaN(v))return '—'; return (dp!=null)?Number(v).toFixed(dp):nf.format(v); }

  var PALETTE = {
    list:   getComputedStyle(document.documentElement).getPropertyValue('--mint').trim(),
    symbol: getComputedStyle(document.documentElement).getPropertyValue('--cyan').trim(),
    number: getComputedStyle(document.documentElement).getPropertyValue('--amber').trim(),
    string: getComputedStyle(document.documentElement).getPropertyValue('--violet').trim()
  };

  fetch('/api/data')
    .then(function(r){ if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); })
    .then(render)
    .catch(function(err){
      $('filemeta').innerHTML = '<b style="color:var(--rose)">Failed to load /api/data: '+err.message+'</b>';
    });

  function render(d){
    var f = d.file||{}, p = d.parser||{}, it = d.interpreter||{}, net = d.net||{};

    // ---- header meta ----
    var kb = (f.size_bytes||0)/1024;
    $('filemeta').innerHTML =
      'FILE <b>'+escapeHtml(f.path||'?')+'</b>' +
      ' · SIZE <b>'+num(kb,2)+' KB</b>' +
      ' · PARSE <b>'+num(f.parse_time_ms,3)+' ms</b>' +
      ' · ROOT <b>#'+(p.root_node||0)+'</b>';
    $('foot-left').textContent = 'KiCad parser dashboard · served from localhost via Winsock';
    $('foot-right').textContent = 'generated ' + new Date().toLocaleString();

    // ---- kpi cards ----
    var cards = [
      ['accent-mint','AST Nodes',   num(p.ast_nodes),       'parsed s-expr nodes'],
      ['accent-mint','Wires',       num(it.wire_count),     'segments'],
      ['accent-amber','Junctions',  num(it.junction_count), 'connection points'],
      ['accent-cyan','Labels',      num(it.label_count),    'net labels'],
      ['accent-violet','Components', num(it.component_count),'symbols'],
      ['accent-cyan','Net Nodes',   num(net.node_count),    'connectivity'],
      ['accent-mint','Net Edges',   num(net.edge_count),    'graph edges'],
      ['accent-amber','Parse Time', num(f.parse_time_ms,3), 'milliseconds']
    ];
    var kc = $('kpis');
    cards.forEach(function(c,i){
      var card = el('div','card '+c[0]+' d'+((i%8)+1));
      card.appendChild(el('div','lab',c[1]));
      card.appendChild(el('div','val',c[2]));
      card.appendChild(el('div','sub',c[3]));
      kc.appendChild(card);
    });

    // ---- ast composition ----
    var bd = p.breakdown||{list:0,symbol:0,number:0,string:0};
    var parts=[['List',bd.list,PALETTE.list],['Symbol',bd.symbol,PALETTE.symbol],
               ['Number',bd.number,PALETTE.number],['String',bd.string,PALETTE.string]];
    var tot = parts.reduce(function(a,x){return a+(x[1]||0);},0);
    $('ast-total').textContent = num(tot)+' nodes';
    var bar = $('ast-bar'); var leg = $('ast-leg');
    parts.forEach(function(x){
      var pct = tot? (x[1]/tot*100):0;
      var seg = el('i'); seg.style.background=x[2]; seg.style.width='0%';
      seg.title = x[0]+': '+x[1];
      bar.appendChild(seg);
      requestAnimationFrame(function(){ seg.style.width = pct+'%'; });

      var row = el('div','leg');
      var k = el('span','k'); k.appendChild(swatch(x[2])); k.appendChild(document.createTextNode(x[0]));
      var mini = el('span','bar-mini'); var mi = el('i'); mi.style.background=x[2]; mi.style.width='0%';
      mini.appendChild(mi);
      var n = el('span','n', num(x[1])+'  ·  '+num(pct,1)+'%');
      row.appendChild(k); row.appendChild(mini); row.appendChild(n);
      leg.appendChild(row);
      requestAnimationFrame(function(){ mi.style.width = pct+'%'; });
    });

    // ---- schematic ----
    drawSchematic(it);

    // ---- connectivity ----
    drawNet(net);
    $('net-hint').textContent = num(net.node_count)+' nodes · '+num(net.edge_count)+' edges';

    // ---- warnings ----
    var wb = $('warnbox');
    if((it.wire_count||0)===0) wb.appendChild(warn('bad','No wires extracted from the schematic.'));
    if((it.junction_count||0)===0) wb.appendChild(warn('bad','No junctions extracted from the schematic.'));
    if((it.wire_count||0)>0 && (it.junction_count||0)>0)
      wb.appendChild(warn('ok','Geometry extracted cleanly. Pipeline nominal.'));

    // ---- tables ----
    buildTables(it, net);
  }

  function swatch(color){ var s=el('i','swatch'); s.style.background=color; return s; }
  function warn(kind,msg){
    var w=el('div','warn '+kind);
    var icon=el('span',null,kind==='ok'?'OK':'!');
    icon.style.fontWeight='700';
    w.appendChild(icon);
    w.appendChild(el('span',null,msg));
    return w;
  }
  function escapeHtml(s){ return String(s).replace(/[&<>"]/g,function(c){
    return ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'})[c]; }); }
)HTML";

        html_content += R"HTML(
  // ---- bounding box over all geometry ----
  function bbox(it){
    var minx=Infinity,miny=Infinity,maxx=-Infinity,maxy=-Infinity,seen=false;
    function acc(x,y){ if(x<minx)minx=x; if(y<miny)miny=y; if(x>maxx)maxx=x; if(x>maxy)maxy=y; seen=true; }
    (it.wires||[]).forEach(function(w){ acc(w.x1,w.y1); acc(w.x2,w.y2); });
    (it.junctions||[]).forEach(function(j){ acc(j.x,j.y); });
    (it.labels||[]).forEach(function(l){ acc(l.x,l.y); });
    (it.components||[]).forEach(function(c){ acc(c.x,c.y); });
    if(!seen) return null;
    if(minx===maxx){minx-=5;maxx+=5;} if(miny===maxy){miny-=5;maxy+=5;}
    return {minx:minx,miny:miny,maxx:maxx,maxy:maxy,w:maxx-minx,h:maxy-miny};
  }

  var SCH = { svg:null, vb:null, vb0:null, diag:1, show:{junc:true,lab:true} };

  function drawSchematic(it){
    var svg = $('schematic'); SCH.svg = svg;
    while(svg.firstChild) svg.removeChild(svg.firstChild);

    var bb = bbox(it);
    if(!bb){ $('canvas-empty').style.display='flex'; return; }
    $('canvas-empty').style.display='none';

    var pad = Math.max(bb.w,bb.h)*0.08 + 4;
    SCH.vb0 = {x:bb.minx-pad, y:bb.miny-pad, w:bb.w+pad*2, h:bb.h+pad*2};
    SCH.vb = Object.assign({},SCH.vb0);
    SCH.diag = Math.sqrt(bb.w*bb.w+bb.h*bb.h)||1;
    applyVB();

    var jr = SCH.diag*0.006 + 0.3;   // junction radius (data units)
    var lr = SCH.diag*0.004 + 0.2;   // label tick radius

    // wires
    var gW = mk('g'); gW.setAttribute('id','g-wires');
    (it.wires||[]).forEach(function(w){
      var ln = mk('line');
      ln.setAttribute('x1',w.x1); ln.setAttribute('y1',w.y1);
      ln.setAttribute('x2',w.x2); ln.setAttribute('y2',w.y2);
      ln.setAttribute('stroke','var(--mint)');
      ln.setAttribute('stroke-width','1.6');
      ln.setAttribute('stroke-linecap','round');
      ln.setAttribute('vector-effect','non-scaling-stroke');
      gW.appendChild(ln);
    });
    svg.appendChild(gW);

    // components (rect + text), if any
    var gC = mk('g'); gC.setAttribute('id','g-comps');
    var box = SCH.diag*0.03 + 1;
    (it.components||[]).forEach(function(c){
      var r = mk('rect');
      r.setAttribute('x',c.x-box/2); r.setAttribute('y',c.y-box/2);
      r.setAttribute('width',box); r.setAttribute('height',box);
      r.setAttribute('rx',box*0.12);
      r.setAttribute('fill','rgba(167,139,250,.12)');
      r.setAttribute('stroke','var(--violet)');
      r.setAttribute('stroke-width','1.4');
      r.setAttribute('vector-effect','non-scaling-stroke');
      gC.appendChild(r);
      if(c.reference){
        var t = mk('text');
        t.setAttribute('x',c.x); t.setAttribute('y',c.y - box*0.62);
        t.setAttribute('fill','var(--violet)'); t.setAttribute('text-anchor','middle');
        t.setAttribute('font-family',"var(--mono)");
        t.setAttribute('font-size', (SCH.diag*0.022+0.8));
        t.textContent = c.reference + (c.value? (' · '+c.value):'');
        gC.appendChild(t);
      }
    });
    svg.appendChild(gC);

    // junctions
    var gJ = mk('g'); gJ.setAttribute('id','g-junc');
    (it.junctions||[]).forEach(function(j){
      var c = mk('circle');
      c.setAttribute('cx',j.x); c.setAttribute('cy',j.y); c.setAttribute('r',jr);
      c.setAttribute('fill','var(--amber)');
      c.setAttribute('stroke','#0a0a0a'); c.setAttribute('stroke-width','0.6');
      c.setAttribute('vector-effect','non-scaling-stroke');
      gJ.appendChild(c);
    });
    svg.appendChild(gJ);

    // labels
    var gL = mk('g'); gL.setAttribute('id','g-lab');
    (it.labels||[]).forEach(function(l){
      var c = mk('circle');
      c.setAttribute('cx',l.x); c.setAttribute('cy',l.y); c.setAttribute('r',lr);
      c.setAttribute('fill','var(--cyan)');
      gL.appendChild(c);
      if(l.name){
        var t = mk('text');
        t.setAttribute('x',l.x+lr*2.2); t.setAttribute('y',l.y);
        t.setAttribute('fill','var(--cyan)'); t.setAttribute('dominant-baseline','middle');
        t.setAttribute('font-family',"var(--mono)");
        t.setAttribute('font-size',(SCH.diag*0.02+0.8));
        t.textContent = l.name;
        gL.appendChild(t);
      }
    });
    svg.appendChild(gL);

    wireInteractions();
  }

  function mk(tag){ return document.createElementNS('http://www.w3.org/2000/svg',tag); }
  function applyVB(){ var v=SCH.vb; SCH.svg.setAttribute('viewBox', v.x+' '+v.y+' '+v.w+' '+v.h); }

  function wireInteractions(){
    var shell = $('canvas-shell'), svg = SCH.svg;
    var dragging=false, lastX=0, lastY=0;

    svg.onwheel = function(e){
      e.preventDefault();
      var rect = svg.getBoundingClientRect();
      var mx = SCH.vb.x + (e.clientX-rect.left)/rect.width  * SCH.vb.w;
      var my = SCH.vb.y + (e.clientY-rect.top)/rect.height * SCH.vb.h;
      var k = e.deltaY>0 ? 1.12 : 0.893;
      var nw = SCH.vb.w*k, nh = SCH.vb.h*k;
      var minw = SCH.vb0.w*0.05, maxw = SCH.vb0.w*8;
      if(nw<minw||nw>maxw) return;
      SCH.vb.x = mx - (mx-SCH.vb.x)*k;
      SCH.vb.y = my - (my-SCH.vb.y)*k;
      SCH.vb.w = nw; SCH.vb.h = nh;
      applyVB();
    };
    shell.onpointerdown = function(e){ dragging=true; lastX=e.clientX; lastY=e.clientY;
      shell.classList.add('dragging'); shell.setPointerCapture(e.pointerId); };
    shell.onpointermove = function(e){
      var rect=svg.getBoundingClientRect();
      var dataX = SCH.vb.x + (e.clientX-rect.left)/rect.width*SCH.vb.w;
      var dataY = SCH.vb.y + (e.clientY-rect.top)/rect.height*SCH.vb.h;
      $('coord').textContent = 'x '+dataX.toFixed(2)+'   y '+dataY.toFixed(2);
      if(!dragging) return;
      var dx=(e.clientX-lastX)/rect.width*SCH.vb.w;
      var dy=(e.clientY-lastY)/rect.height*SCH.vb.h;
      SCH.vb.x-=dx; SCH.vb.y-=dy; lastX=e.clientX; lastY=e.clientY; applyVB();
    };
    function endDrag(){ dragging=false; shell.classList.remove('dragging'); }
    shell.onpointerup = endDrag; shell.onpointerleave = endDrag;

    $('btn-fit').onclick = function(){ SCH.vb = Object.assign({},SCH.vb0); applyVB(); };
    toggle('btn-grid', function(on){ shell.classList.toggle('nogrid', !on); });
    toggle('btn-junc', function(on){ var g=$('g-junc'); if(g) g.style.display = on?'':'none'; });
    toggle('btn-lab',  function(on){ var g=$('g-lab');  if(g) g.style.display = on?'':'none'; });
  }
  function toggle(id, fn){
    var b=$(id); b.onclick=function(){ var on=!b.classList.contains('on');
      b.classList.toggle('on',on); fn(on); }; }

  // ---- connectivity graph ----
  function drawNet(net){
    var svg=$('netgraph'); while(svg.firstChild) svg.removeChild(svg.firstChild);
    var nodes=net.nodes||[], edges=net.edges||[];
    if(!nodes.length){ $('net-empty').style.display='flex'; return; }
    $('net-empty').style.display='none';

    var minx=Infinity,miny=Infinity,maxx=-Infinity,maxy=-Infinity;
    nodes.forEach(function(n){ if(n.x<minx)minx=n.x; if(n.y<miny)miny=n.y;
      if(n.x>maxx)maxx=n.x; if(n.y>maxy)maxy=n.y; });
    if(minx===maxx){minx-=5;maxx+=5;} if(miny===maxy){miny-=5;maxy+=5;}
    var w=maxx-minx, h=maxy-miny, pad=Math.max(w,h)*0.08+2;
    svg.setAttribute('viewBox',(minx-pad)+' '+(miny-pad)+' '+(w+pad*2)+' '+(h+pad*2));
    var diag=Math.sqrt(w*w+h*h)||1;

    var byId={}; nodes.forEach(function(n){ byId[n.id]=n; });
    edges.forEach(function(e){
      var a=byId[e.start], b=byId[e.end]; if(!a||!b) return;
      var ln=mk('line'); ln.setAttribute('x1',a.x); ln.setAttribute('y1',a.y);
      ln.setAttribute('x2',b.x); ln.setAttribute('y2',b.y);
      ln.setAttribute('stroke','var(--cyan)'); ln.setAttribute('stroke-opacity','.45');
      ln.setAttribute('stroke-width','1'); ln.setAttribute('vector-effect','non-scaling-stroke');
      svg.appendChild(ln);
    });
    nodes.forEach(function(n){
      var c=mk('circle'); c.setAttribute('cx',n.x); c.setAttribute('cy',n.y);
      c.setAttribute('r',diag*0.012+0.4);
      c.setAttribute('fill','var(--cyan)'); c.setAttribute('fill-opacity','.9');
      svg.appendChild(c);
    });
  }

  // ---- tables ----
  var TABLES, CUR='components';
  function buildTables(it, net){
    TABLES = {
      components:{ label:'Components', count:(it.components||[]).length,
        cols:['#','Ref','Value','X','Y','Rot'],
        rows:(it.components||[]).map(function(c,i){return ['idx:'+i,'tag:'+(c.reference||'—'),(c.value||'—'),num(c.x,2),num(c.y,2),num(c.rotation,1)];}) },
      labels:{ label:'Labels', count:(it.labels||[]).length,
        cols:['#','Name','X','Y'],
        rows:(it.labels||[]).map(function(l,i){return ['idx:'+i,'tag:'+(l.name||'—'),num(l.x,2),num(l.y,2)];}) },
      wires:{ label:'Wires', count:(it.wires||[]).length,
        cols:['#','X1','Y1','X2','Y2','Length'],
        rows:(it.wires||[]).map(function(w,i){var dx=w.x2-w.x1,dy=w.y2-w.y1;
          return ['idx:'+i,num(w.x1,2),num(w.y1,2),num(w.x2,2),num(w.y2,2),num(Math.sqrt(dx*dx+dy*dy),2)];}) },
      junctions:{ label:'Junctions', count:(it.junctions||[]).length,
        cols:['#','X','Y'],
        rows:(it.junctions||[]).map(function(j,i){return ['idx:'+i,num(j.x,2),num(j.y,2)];}) },
      edges:{ label:'Net Edges', count:(net.edges||[]).length,
        cols:['#','Start','End'],
        rows:(net.edges||[]).map(function(e,i){return ['idx:'+e.id,num(e.start),num(e.end)];}) }
    };
    var tabs=$('tabs'); tabs.innerHTML='';
    Object.keys(TABLES).forEach(function(key){
      var t=TABLES[key];
      var b=el('div','tab'+(key===CUR?' on':''));
      b.innerHTML = t.label+'<span class="c">'+t.count+'</span>';
      b.onclick=function(){ CUR=key;
        [].forEach.call(tabs.children,function(x){x.classList.remove('on');});
        b.classList.add('on'); renderTable(); };
      tabs.appendChild(b);
    });
    renderTable();
  }
  function renderTable(){
    var t=TABLES[CUR], host=$('table');
    $('table-count').textContent = t.count+' rows';
    if(!t.rows.length){ host.innerHTML='<div class="tnote center">— empty —</div>'; return; }
    var LIMIT=500, rows=t.rows.slice(0,LIMIT);
    var html='<table><thead><tr>';
    t.cols.forEach(function(c){ html+='<th>'+c+'</th>'; });
    html+='</tr></thead><tbody>';
    rows.forEach(function(r){
      html+='<tr>';
      r.forEach(function(cell){
        var c=String(cell), cls='';
        if(c.indexOf('idx:')===0){cls=' class="idx"';c=c.slice(4);}
        else if(c.indexOf('tag:')===0){cls=' class="tag"';c=c.slice(4);}
        html+='<td'+cls+'>'+escapeHtml(c)+'</td>';
      });
      html+='</tr>';
    });
    html+='</tbody></table>';
    if(t.rows.length>LIMIT) html+='<div class="tnote">showing first '+LIMIT+' of '+t.rows.length+' rows</div>';
    host.innerHTML=html;
  }
})();
</script>
</body>
</html>
)HTML";

        return html_content;
    }
    // ----- HTTP response framing -----------------------------
    std::string BuildResponse(
        const std::string& status,
        const std::string& contentType,
        const std::string& body)
    {
        std::ostringstream r;
        r << "HTTP/1.1 " << status << "\r\n";
        r << "Content-Type: " << contentType << "\r\n";
        r << "Content-Length: " << body.size() << "\r\n";
        r << "Cache-Control: no-store\r\n";
        r << "Access-Control-Allow-Origin: *\r\n";
        r << "Connection: close\r\n";
        r << "\r\n";
        r << body;
        return r.str();
    }

    void SendAll(SOCKET sock, const std::string& data)
    {
        std::size_t total = 0;
        while (total < data.size())
        {
            int sent = send(
                sock,
                data.data() + total,
                static_cast<int>(data.size() - total),
                0);
            if (sent == SOCKET_ERROR)
                return;
            total += static_cast<std::size_t>(sent);
        }
    }

    // ----- Per-connection handler (own thread) ---------------
    void HandleClient(
        SOCKET client,
        const std::string& json,
        const std::string& html)
    {
        // recv timeout so a stalled/speculative socket can't hang a thread forever
        DWORD timeout_ms = 5000;
        setsockopt(
            client, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

        char buf[8192];
        int received = recv(client, buf, sizeof(buf) - 1, 0);
        if (received <= 0)
        {
            closesocket(client);
            return;
        }

        std::string request(buf, static_cast<std::size_t>(received));

        // Parse the request line: METHOD SP TARGET SP VERSION
        std::string target = "/";
        std::size_t sp1 = request.find(' ');
        if (sp1 != std::string::npos)
        {
            std::size_t sp2 = request.find(' ', sp1 + 1);
            if (sp2 != std::string::npos)
                target = request.substr(sp1 + 1, sp2 - sp1 - 1);
        }

        std::string response;
        if (target == "/api/data")
            response = BuildResponse("200 OK", "application/json; charset=utf-8", json);
        else if (target == "/favicon.ico")
            response = BuildResponse("204 No Content", "text/plain", "");
        else
            response = BuildResponse("200 OK", "text/html; charset=utf-8", html);

        SendAll(client, response);
        closesocket(client);
    }
} // anonymous namespace

// ------------------------------------------------------------
//  Public entry point
// ------------------------------------------------------------
int WebDashboard::Serve(
    const WebDashboardData& data,
    int port,
    bool open_browser)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "[web] WSAStartup failed.\n";
        return 1;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET)
    {
        std::cerr << "[web] socket() failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    BOOL reuse = TRUE;
    setsockopt(
        listen_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // bind to 127.0.0.1 only

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[web] bind() failed on port " << port
            << " (" << WSAGetLastError() << "). Is the port already in use?\n";
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "[web] listen() failed: " << WSAGetLastError() << "\n";
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    // Build payloads once for the whole session (data is immutable here).
    // These locals outlive every worker thread because this function never
    // returns (the accept loop runs until the process is killed).
    const std::string json = BuildJson(data);
    const std::string html = BuildHtml();

    std::string url = "http://localhost:" + std::to_string(port) + "/";

    std::cout << "\n========================================================\n";
    std::cout << " WEB DASHBOARD\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "URL        : " << url << "\n";
    std::cout << "Bind       : 127.0.0.1:" << port << " (local only)\n";
    std::cout << "Endpoints  : /  and  /api/data\n";
    std::cout << "Stop       : press Ctrl+C in this window\n";
    std::cout << "========================================================\n";

    if (open_browser)
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    for (;;)
    {
        SOCKET client = accept(listen_sock, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            continue; // transient accept error; keep serving

        // One detached worker per connection (responses set Connection: close).
        std::thread(HandleClient, client, std::cref(json), std::cref(html)).detach();
    }
}