#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>
#include <ctime>

using json = nlohmann::json;

namespace neuroswarm {

class VisualizerLobe {
public:
    VisualizerLobe(const std::string& thalamus_ip = "localhost")
        : ctx(1), sub(ctx, zmq::socket_type::sub) {

        sub.connect("tcp://" + thalamus_ip + ":5556");
        sub.set(zmq::sockopt::subscribe, "");

        std::cout << "[VISUALIZER] EEG Subsystem online. Web Dashboard on http://localhost:8080" << std::endl;
    }

    void start() {
        std::thread zmq_thread(&VisualizerLobe::listen_zmq, this);
        zmq_thread.detach();

        httplib::Server svr;

        svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(get_dashboard_html(), "text/html");
        });

        svr.Get("/events", [this](const httplib::Request&, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(event_mutex);
            json data = event_buffer;
            res.set_content(data.dump(), "application/json");
        });

        svr.listen("0.0.0.0", 8080);
    }

private:
    zmq::context_t ctx;
    zmq::socket_t sub;
    std::mutex event_mutex;
    std::deque<json> event_buffer;
    const size_t max_events = 300;

    void listen_zmq() {
        while (true) {
            zmq::message_t msg;
            if (sub.recv(msg, zmq::recv_flags::none)) {
                try {
                    auto j = json::parse(static_cast<char*>(msg.data()), static_cast<char*>(msg.data()) + msg.size());
                    std::lock_guard<std::mutex> lock(event_mutex);
                    j["ui_ts"] = std::time(nullptr);
                    event_buffer.push_front(j);
                    if (event_buffer.size() > max_events) event_buffer.pop_back();
                } catch (...) {}
            }
        }
    }

    std::string get_dashboard_html() {
        return R"NEURO_HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>NEUROSWARM // NEURAL CORTEX v2.1</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@300;400;700;800&display=swap');
:root {
    --bg: #06060c;
    --panel-bg: rgba(8,8,16,0.95);
    --panel-border: rgba(255,255,255,0.06);
    --accent: #00d4ff;
    --accent-dim: rgba(0,212,255,0.12);
    --text: #c8d8e8;
    --text-dim: #3a4a5a;
    --font: 'JetBrains Mono','Courier New',monospace;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font);
    display: grid;
    grid-template-columns: 280px 1fr 300px;
    height: 100vh;
    overflow: hidden;
}
::-webkit-scrollbar { width: 3px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: rgba(0,212,255,0.2); border-radius: 2px; }

/* ── LEFT PANEL ── */
#left-panel {
    background: var(--panel-bg);
    border-right: 1px solid var(--panel-border);
    display: flex;
    flex-direction: column;
    padding: 18px 14px;
    gap: 14px;
    overflow-y: auto;
    z-index: 10;
}
.logo-block { text-align: center; padding-bottom: 14px; border-bottom: 1px solid var(--panel-border); }
.logo-title {
    font-size: 22px; font-weight: 800; letter-spacing: 3px;
    color: var(--accent);
    text-shadow: 0 0 18px rgba(0,212,255,0.8), 0 0 40px rgba(0,212,255,0.3);
}
.logo-sub { font-size: 9px; color: var(--text-dim); letter-spacing: 4px; margin-top: 4px; text-transform: uppercase; }

.section-label {
    font-size: 9px; font-weight: 700; letter-spacing: 3px;
    color: var(--text-dim); text-transform: uppercase;
    margin-bottom: 8px;
}

/* Lobe list */
#lobe-list { display: flex; flex-direction: column; gap: 5px; }
.lobe-row {
    display: flex; align-items: center; gap: 8px;
    padding: 6px 8px; border-radius: 4px;
    background: rgba(255,255,255,0.02);
    border: 1px solid rgba(255,255,255,0.03);
    transition: background 0.3s;
    position: relative; overflow: hidden;
}
.lobe-row.active { background: rgba(0,212,255,0.05); border-color: rgba(0,212,255,0.15); }
.lobe-dot {
    width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0;
    transition: box-shadow 0.3s;
}
.lobe-dot.pulse { animation: dotPulse 0.8s ease-out; }
@keyframes dotPulse {
    0%   { transform: scale(1); }
    50%  { transform: scale(1.8); }
    100% { transform: scale(1); }
}
.lobe-info { flex: 1; min-width: 0; }
.lobe-name { font-size: 10px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.lobe-desc { font-size: 8px; color: var(--text-dim); }
.lobe-status { font-size: 8px; font-weight: 700; letter-spacing: 1px; }
.lobe-status.idle { color: var(--text-dim); }
.lobe-status.active { color: var(--accent); text-shadow: 0 0 6px var(--accent); }
.pulse-ring {
    position: absolute; right: 6px; top: 50%; transform: translateY(-50%);
    width: 12px; height: 12px; border-radius: 50%;
    border: 1px solid currentColor; opacity: 0;
}
.lobe-row.active .pulse-ring { animation: ringPulse 1s infinite; }
@keyframes ringPulse {
    0%   { transform: translateY(-50%) scale(0.8); opacity: 0.8; }
    100% { transform: translateY(-50%) scale(2.0); opacity: 0; }
}

/* Bottom stats */
.stat-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.stat-box {
    background: rgba(0,0,0,0.4); border: 1px solid var(--panel-border);
    border-radius: 4px; padding: 8px; text-align: center;
}
.stat-val { font-size: 18px; font-weight: 800; color: var(--accent); text-shadow: 0 0 10px rgba(0,212,255,0.5); }
.stat-lbl { font-size: 8px; color: var(--text-dim); letter-spacing: 1px; text-transform: uppercase; margin-top: 2px; }

.stress-bar-wrap { margin-top: 2px; }
.bar-track { height: 4px; background: rgba(255,255,255,0.05); border-radius: 2px; overflow: hidden; margin-top: 4px; }
.bar-fill { height: 100%; border-radius: 2px; transition: width 0.5s, background 0.5s; }

/* ── CENTER VIEWPORT ── */
#center { position: relative; overflow: hidden; background: #06060c; }
#brain-canvas { display: block; width: 100%; height: 100%; }

/* Scanline overlay */
#scanlines {
    position: absolute; inset: 0; pointer-events: none; z-index: 5;
    background: repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(0,0,0,0.03) 2px, rgba(0,0,0,0.03) 4px);
}
/* Radial vignette */
#vignette {
    position: absolute; inset: 0; pointer-events: none; z-index: 4;
    background: radial-gradient(ellipse 70% 65% at 50% 50%, transparent 40%, rgba(6,6,12,0.6) 100%);
}

/* 2D lobe labels */
.lobe-label-3d {
    position: absolute; pointer-events: none; z-index: 6;
    font-family: var(--font); font-size: 9px; font-weight: 700;
    letter-spacing: 1px; text-transform: uppercase;
    padding: 3px 6px; border-radius: 2px;
    background: rgba(6,6,12,0.75); border: 1px solid rgba(255,255,255,0.08);
    white-space: nowrap; transition: opacity 0.4s;
    transform: translate(-50%, -50%);
}

/* Bottom HUD */
#bottom-hud {
    position: absolute; bottom: 0; left: 0; right: 0;
    display: flex; align-items: center; justify-content: space-between;
    padding: 10px 20px;
    background: linear-gradient(to top, rgba(6,6,12,0.9), transparent);
    z-index: 8; pointer-events: none;
}
.hud-item { font-size: 10px; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; }
.hud-live { color: var(--accent); display: flex; align-items: center; gap: 6px; }
.live-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--accent); animation: livePulse 1.2s infinite; }
@keyframes livePulse { 0%,100% { opacity:1; box-shadow:0 0 4px var(--accent); } 50% { opacity:0.3; box-shadow:none; } }
.hud-mode { color: #a0b0c0; text-align: center; }
.hud-mode span { color: var(--accent); }
.hud-rate { color: var(--text-dim); }

/* ── RIGHT PANEL ── */
#right-panel {
    background: var(--panel-bg);
    border-left: 1px solid var(--panel-border);
    display: flex; flex-direction: column;
    padding: 18px 14px; gap: 14px;
    overflow-y: auto; z-index: 10;
}
.r-section { display: flex; flex-direction: column; gap: 6px; }
.focus-text {
    font-size: 11px; color: #00ffaa; line-height: 1.4;
    padding: 8px; background: rgba(0,255,170,0.04);
    border-left: 2px solid #00ffaa; border-radius: 2px;
    min-height: 36px;
}
.thought-text {
    font-size: 10px; color: #888; font-style: italic; line-height: 1.5;
    padding: 8px; background: rgba(255,255,255,0.02);
    border-left: 2px solid rgba(255,255,255,0.1); border-radius: 2px;
    min-height: 36px;
}

/* Neural stream */
#neural-stream { flex: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 3px; min-height: 0; }
.stream-entry {
    display: flex; align-items: flex-start; gap: 6px;
    padding: 4px 6px; border-radius: 3px;
    background: rgba(255,255,255,0.015);
    animation: streamIn 0.25s ease-out;
    font-size: 9px;
}
@keyframes streamIn { from { opacity:0; transform:translateX(10px); } to { opacity:1; transform:none; } }
.stream-dot { width: 5px; height: 5px; border-radius: 50%; flex-shrink:0; margin-top:3px; }
.stream-body { flex:1; min-width:0; }
.stream-route { font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.stream-time { color: var(--text-dim); font-size: 8px; flex-shrink:0; }

/* Tasks */
#ralph-tasks { display: flex; flex-direction: column; gap: 4px; max-height: 100px; overflow-y: auto; }
.task-entry {
    font-size: 9px; padding: 4px 8px; border-radius: 3px;
    background: rgba(255,255,255,0.02); border-left: 2px solid var(--text-dim);
}
.task-entry.done { border-left-color: #00ffaa; color: #00ffaa; }
.task-entry.fail { border-left-color: #ef4444; color: #ef4444; }
</style>
</head>
<body>

<!-- LEFT PANEL -->
<div id="left-panel">
    <div class="logo-block">
        <div class="logo-title">NEUROSWARM</div>
        <div class="logo-sub">Neural Cortex v2.1</div>
    </div>

    <div>
        <div class="section-label">Cortical Activity</div>
        <div id="lobe-list"></div>
    </div>

    <div style="margin-top:auto;">
        <div class="section-label">System Vitals</div>
        <div class="stat-grid">
            <div class="stat-box">
                <div class="stat-val" id="stat-bus">0</div>
                <div class="stat-lbl">msg/s</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-rem">0</div>
                <div class="stat-lbl">REM cycles</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-success" style="color:#00ffaa;text-shadow:0 0 10px rgba(0,255,170,0.5)">—</div>
                <div class="stat-lbl">Success Rate</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-stress" style="color:#ef4444;text-shadow:0 0 10px rgba(239,68,68,0.5)">—</div>
                <div class="stat-lbl">Stress</div>
            </div>
        </div>
        <div class="stress-bar-wrap" style="margin-top:10px;">
            <div class="section-label" style="margin-bottom:4px;">Cognitive Load</div>
            <div class="bar-track"><div class="bar-fill" id="stress-bar" style="width:0%;background:#ef4444;"></div></div>
        </div>
    </div>
</div>

<!-- CENTER: 3D BRAIN -->
<div id="center">
    <canvas id="brain-canvas"></canvas>
    <div id="scanlines"></div>
    <div id="vignette"></div>
    <div id="label-container"></div>
    <div id="bottom-hud">
        <div class="hud-item hud-live"><div class="live-dot"></div>BUS LIVE</div>
        <div class="hud-item hud-mode">MODE: <span id="hud-mode-val">REALITY</span></div>
        <div class="hud-item hud-rate"><span id="hud-rate-val">0</span> MSG/S</div>
    </div>
</div>

<!-- RIGHT PANEL -->
<div id="right-panel">
    <div class="r-section">
        <div class="section-label">Cognitive Focus</div>
        <div class="focus-text" id="active-goal">Idle — awaiting directive...</div>
    </div>
    <div class="r-section">
        <div class="section-label">Last Thought</div>
        <div class="thought-text" id="last-thought">Standby.</div>
    </div>
    <div class="r-section" style="flex:1;min-height:0;display:flex;flex-direction:column;">
        <div class="section-label">Neural Stream</div>
        <div id="neural-stream"></div>
    </div>
    <div class="r-section">
        <div class="section-label">Ralph Tasks</div>
        <div id="ralph-tasks"><div style="color:var(--text-dim);font-size:9px;">No tasks yet.</div></div>
    </div>
</div>

<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<script>
'use strict';

/* ═══════════════════════════════════════════════════════
   LOBE DEFINITIONS
═══════════════════════════════════════════════════════ */
const LOBES = {
    frontal:    { name:"Frontal Executive",  component:"frontal_executive",   color:0x00d4ff, hex:"#00d4ff", pos:[0,1.8,2.8],    scale:[2.2,1.8,2.0], desc:"Planning & Goals" },
    parietal:   { name:"Synaptic Cortex",    component:"synaptic_controller", color:0xa855f7, hex:"#a855f7", pos:[0,3.1,0],      scale:[2.5,1.2,2.0], desc:"LLM Inference" },
    temporal_l: { name:"Hippocampus",        component:"hippocampus",         color:0xf97316, hex:"#f97316", pos:[-3.5,0,0.5],   scale:[1.2,1.3,2.2], desc:"Semantic Memory" },
    temporal_r: { name:"Wernicke Cortex",    component:"wernicke_lobe",       color:0xfb923c, hex:"#fb923c", pos:[3.5,0,0.5],    scale:[1.2,1.3,2.2], desc:"Language NLU" },
    occipital:  { name:"Visual Cortex",      component:"visual_lobe",         color:0x14b8a6, hex:"#14b8a6", pos:[0,0.8,-3.5],   scale:[2.2,1.8,1.2], desc:"Visual Input" },
    cerebellum: { name:"Motor Cortex",       component:"motor_cortex",        color:0x22c55e, hex:"#22c55e", pos:[0,-2.5,-2.2],  scale:[2.0,1.0,1.6], desc:"Execution" },
    brainstem:  { name:"Thalamus",           component:"thalamus",            color:0x6366f1, hex:"#6366f1", pos:[0,-3.2,-0.3],  scale:[0.8,1.8,0.8], desc:"Neural Relay" },
    cingulate:  { name:"Critic Lobe",        component:"critic_lobe",         color:0xf59e0b, hex:"#f59e0b", pos:[0,2.5,0.5],    scale:[1.5,0.7,2.8], desc:"Safety Check" },
    broca:      { name:"Broca Area",         component:"broca_terminal",      color:0x84cc16, hex:"#84cc16", pos:[-2.2,0.5,2.5], scale:[1.0,1.0,1.0], desc:"User Interface" },
    autonomic:  { name:"Homeostasis",        component:"homeostasis",         color:0xef4444, hex:"#ef4444", pos:[1.5,-1.5,0.5], scale:[1.2,1.2,1.2], desc:"Stress Monitor" }
};

const ORIGIN_LOBE = {
    'frontal_executive':'frontal', 'synaptic_controller':'parietal',
    'motor_cortex':'cerebellum',   'critic_lobe':'cingulate',
    'hippocampus':'temporal_l',    'rem_engine':'parietal',
    'homeostasis':'autonomic',     'visual_lobe':'occipital',
    'broca_terminal':'broca',      'user_terminal':'broca',
    'thalamus':'brainstem',        'wernicke_lobe':'temporal_r',
    'metacognition':'parietal',    'amygdala':'autonomic'
};
const INTENT_TARGET = {
    'inference_request':'parietal',    'execution_request':'cerebellum',
    'execution_result':'frontal',      'critic_validate':'cingulate',
    'critic_result':'frontal',         'search_memory':'temporal_l',
    'search_result':'frontal',         'homeostatic_pulse':'frontal',
    'embedding_request':'temporal_l',  'visual_stimulus':'frontal',
    'stimulus':'frontal',              'prompt_update':'frontal',
};

/* ═══════════════════════════════════════════════════════
   BUILD LEFT PANEL LOBE LIST
═══════════════════════════════════════════════════════ */
const lobeListEl = document.getElementById('lobe-list');
const lobeRowEls = {};
Object.entries(LOBES).forEach(([key, L]) => {
    const row = document.createElement('div');
    row.className = 'lobe-row';
    row.id = 'lobe-row-' + key;
    row.innerHTML = `
        <div class="lobe-dot" id="dot-${key}" style="background:${L.hex};box-shadow:0 0 4px ${L.hex}44;"></div>
        <div class="lobe-info">
            <div class="lobe-name" style="color:${L.hex}">${L.name}</div>
            <div class="lobe-desc">${L.desc}</div>
        </div>
        <div class="lobe-status idle" id="status-${key}">IDLE</div>
        <div class="pulse-ring" style="color:${L.hex};border-color:${L.hex};"></div>
    `;
    lobeListEl.appendChild(row);
    lobeRowEls[key] = row;
});

/* ═══════════════════════════════════════════════════════
   THREE.JS SETUP
═══════════════════════════════════════════════════════ */
const canvas = document.getElementById('brain-canvas');
const center = document.getElementById('center');

const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setClearColor(0x06060c, 1);

const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(0x06060c, 0.035);

const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 200);
camera.position.set(0, 2, 18);
camera.lookAt(0, 0, 0);

function resizeRenderer() {
    const w = center.clientWidth, h = center.clientHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
}
resizeRenderer();
window.addEventListener('resize', resizeRenderer);

/* ── Lighting ── */
scene.add(new THREE.AmbientLight(0x112233, 0.6));
const keyLight = new THREE.PointLight(0x00d4ff, 1.2, 30);
keyLight.position.set(6, 8, 10);
scene.add(keyLight);
const fillLight = new THREE.PointLight(0xa855f7, 0.6, 30);
fillLight.position.set(-8, -4, -6);
scene.add(fillLight);
const rimLight = new THREE.PointLight(0x14b8a6, 0.4, 25);
rimLight.position.set(0, -6, -8);
scene.add(rimLight);

/* ── OrbitControls (inline minimal implementation) ── */
let isDragging = false, lastMouse = {x:0,y:0};
let spherical = {theta: 0.3, phi: 1.3, r: 18};
let autoRotate = true, userInteractTimer = null;
const brainGroup = new THREE.Group();
scene.add(brainGroup);

canvas.addEventListener('mousedown', e => {
    isDragging = true; lastMouse = {x:e.clientX, y:e.clientY};
    autoRotate = false;
    clearTimeout(userInteractTimer);
});
canvas.addEventListener('mousemove', e => {
    if (!isDragging) return;
    const dx = e.clientX - lastMouse.x, dy = e.clientY - lastMouse.y;
    spherical.theta -= dx * 0.008;
    spherical.phi   = Math.max(0.3, Math.min(Math.PI - 0.3, spherical.phi + dy * 0.008));
    lastMouse = {x:e.clientX, y:e.clientY};
});
canvas.addEventListener('mouseup', () => {
    isDragging = false;
    userInteractTimer = setTimeout(() => { autoRotate = true; }, 3000);
});
canvas.addEventListener('mouseleave', () => {
    isDragging = false;
    userInteractTimer = setTimeout(() => { autoRotate = true; }, 3000);
});
canvas.addEventListener('wheel', e => {
    spherical.r = Math.max(8, Math.min(35, spherical.r + e.deltaY * 0.04));
    autoRotate = false;
    clearTimeout(userInteractTimer);
    userInteractTimer = setTimeout(() => { autoRotate = true; }, 3000);
}, {passive:true});

function updateCamera() {
    const x = spherical.r * Math.sin(spherical.phi) * Math.sin(spherical.theta);
    const y = spherical.r * Math.cos(spherical.phi);
    const z = spherical.r * Math.sin(spherical.phi) * Math.cos(spherical.theta);
    camera.position.set(x, y, z);
    camera.lookAt(0, 0, 0);
}

/* ═══════════════════════════════════════════════════════
   BUILD 3D BRAIN
═══════════════════════════════════════════════════════ */

/* Outer shell */
(function buildShell() {
    const geo = new THREE.SphereGeometry(1, 128, 128);
    const pos = geo.attributes.position;
    for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
        const n = 1 + 0.04*Math.sin(x*2.3) + 0.04*Math.cos(y*1.7) + 0.04*Math.sin(z*2.1);
        pos.setXYZ(i, x*n, y*n, z*n);
    }
    pos.needsUpdate = true;
    geo.computeVertexNormals();
    geo.scale(4.5, 3.8, 4.0);

    const shellMat = new THREE.MeshPhysicalMaterial({
        color: 0x112233, transparent: true, opacity: 0.07,
        roughness: 0.8, metalness: 0.0, side: THREE.DoubleSide
    });
    brainGroup.add(new THREE.Mesh(geo, shellMat));

    const wireMat = new THREE.MeshBasicMaterial({
        color: 0x00d4ff, wireframe: true, transparent: true, opacity: 0.04
    });
    brainGroup.add(new THREE.Mesh(geo.clone(), wireMat));
})();

/* Lobe meshes */
const lobeMeshes  = {};
const lobeGlows   = {};
const lobeActivity= {};   // { intensity, decay }
const lobeVec3    = {};   // cached THREE.Vector3 for arc spawning

Object.entries(LOBES).forEach(([key, L]) => {
    const geo = new THREE.SphereGeometry(1, 32, 32);

    const mesh = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({
        color: L.color, emissive: L.color, emissiveIntensity: 0,
        transparent: true, opacity: 0.22,
        roughness: 0.5, metalness: 0.1
    }));
    mesh.position.set(...L.pos);
    mesh.scale.set(...L.scale);
    brainGroup.add(mesh);
    lobeMeshes[key] = mesh;

    const glow = new THREE.Mesh(geo.clone(), new THREE.MeshBasicMaterial({
        color: L.color, transparent: true, opacity: 0,
        blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.FrontSide
    }));
    glow.position.set(...L.pos);
    glow.scale.set(L.scale[0]*1.5, L.scale[1]*1.5, L.scale[2]*1.5);
    brainGroup.add(glow);
    lobeGlows[key] = glow;

    lobeActivity[key] = { intensity: 0 };
    lobeVec3[key] = new THREE.Vector3(...L.pos);
});

/* ── Particle field (ambient neural dust) ── */
(function buildDust() {
    const count = 800;
    const verts = new Float32Array(count * 3);
    for (let i = 0; i < count; i++) {
        verts[i*3]   = (Math.random()-0.5)*16;
        verts[i*3+1] = (Math.random()-0.5)*13;
        verts[i*3+2] = (Math.random()-0.5)*13;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(verts, 3));
    const mat = new THREE.PointsMaterial({ color:0x00d4ff, size:0.04, transparent:true, opacity:0.25 });
    scene.add(new THREE.Points(geo, mat));
})();

/* ═══════════════════════════════════════════════════════
   LOBE ACTIVATION
═══════════════════════════════════════════════════════ */
function activateLobe(key) {
    if (!lobeMeshes[key]) return;
    lobeActivity[key].intensity = 1.0;

    // Left panel update
    const row    = document.getElementById('lobe-row-' + key);
    const dot    = document.getElementById('dot-' + key);
    const status = document.getElementById('status-' + key);
    if (row)    { row.classList.add('active'); }
    if (dot)    { dot.classList.remove('pulse'); void dot.offsetWidth; dot.classList.add('pulse'); }
    if (status) { status.textContent = 'ACTIVE'; status.className = 'lobe-status active'; }
}

function decayLobes(dt) {
    const DECAY = dt / 2500;  // 2.5 s
    Object.entries(lobeActivity).forEach(([key, act]) => {
        if (act.intensity <= 0) return;
        act.intensity = Math.max(0, act.intensity - DECAY);
        const t = act.intensity;

        lobeMeshes[key].material.emissiveIntensity = t;
        lobeGlows[key].material.opacity = t * 0.3;

        if (t <= 0) {
            const row    = document.getElementById('lobe-row-' + key);
            const status = document.getElementById('status-' + key);
            if (row)    row.classList.remove('active');
            if (status) { status.textContent = 'IDLE'; status.className = 'lobe-status idle'; }
        }
    });
}

/* ═══════════════════════════════════════════════════════
   NEURAL ARC PARTICLES
═══════════════════════════════════════════════════════ */
const arcGroup = new THREE.Group();
scene.add(arcGroup);
const activeArcs = [];

function spawnArc(srcKey, dstKey) {
    if (!lobeVec3[srcKey] || !lobeVec3[dstKey]) return;
    const src = lobeVec3[srcKey].clone();
    const dst = lobeVec3[dstKey].clone();
    const mid = src.clone().lerp(dst, 0.5).add(new THREE.Vector3(
        (Math.random()-0.5)*2, Math.random()*3+2, (Math.random()-0.5)*2
    ));

    const curve = new THREE.QuadraticBezierCurve3(src, mid, dst);
    const color = LOBES[srcKey] ? LOBES[srcKey].color : 0x00d4ff;
    const particleCount = 8;
    const particles = [];

    for (let i = 0; i < particleCount; i++) {
        const geo = new THREE.SphereGeometry(0.09, 6, 6);
        const mat = new THREE.MeshBasicMaterial({
            color, transparent: true, opacity: 0.9,
            blending: THREE.AdditiveBlending, depthWrite: false
        });
        const mesh = new THREE.Mesh(geo, mat);
        arcGroup.add(mesh);
        particles.push({ mesh, offset: i / particleCount });
    }

    activeArcs.push({ curve, particles, t: 0, duration: 700, color });
}

function updateArcs(dt) {
    for (let i = activeArcs.length - 1; i >= 0; i--) {
        const arc = activeArcs[i];
        arc.t += dt;
        const progress = Math.min(arc.t / arc.duration, 1);

        arc.particles.forEach(({ mesh, offset }) => {
            const p = (progress + offset) % 1;
            const pt = arc.curve.getPoint(p);
            mesh.position.copy(pt);
            const fade = Math.sin(p * Math.PI);
            mesh.material.opacity = fade * 0.9;
        });

        if (progress >= 1) {
            arc.particles.forEach(({ mesh }) => { arcGroup.remove(mesh); mesh.geometry.dispose(); mesh.material.dispose(); });
            activeArcs.splice(i, 1);
        }
    }
}

/* ═══════════════════════════════════════════════════════
   2D LABEL OVERLAY
═══════════════════════════════════════════════════════ */
const labelContainer = document.getElementById('label-container');
const labelEls = {};
const projVec = new THREE.Vector3();

Object.entries(LOBES).forEach(([key, L]) => {
    const el = document.createElement('div');
    el.className = 'lobe-label-3d';
    el.textContent = L.name;
    el.style.color = L.hex;
    el.style.borderColor = L.hex + '33';
    el.style.opacity = '0';
    labelContainer.appendChild(el);
    labelEls[key] = el;
});

function updateLabels() {
    const w = center.clientWidth, h = center.clientHeight;
    Object.entries(LOBES).forEach(([key, L]) => {
        projVec.set(...L.pos);
        // apply brainGroup rotation
        projVec.applyMatrix4(brainGroup.matrixWorld);
        projVec.project(camera);

        const x = (projVec.x * 0.5 + 0.5) * w;
        const y = (-(projVec.y) * 0.5 + 0.5) * h;
        const el = labelEls[key];
        el.style.left = x + 'px';
        el.style.top  = y + 'px';

        // fade in when lobe is active, dim otherwise
        const act = lobeActivity[key].intensity;
        el.style.opacity = (0.3 + act * 0.7).toFixed(2);
        el.style.textShadow = act > 0.1 ? `0 0 8px ${L.hex}` : 'none';

        // hide if behind camera
        el.style.display = projVec.z < 1 ? 'block' : 'none';
    });
}

/* ═══════════════════════════════════════════════════════
   IDLE NEURAL SPARKS
═══════════════════════════════════════════════════════ */
let idleSparkTimer = 0;
function tickIdleSparks(dt) {
    idleSparkTimer += dt;
    if (idleSparkTimer > 3000 + Math.random()*2000) {
        idleSparkTimer = 0;
        const keys = Object.keys(LOBES);
        const a = keys[Math.floor(Math.random()*keys.length)];
        let b = keys[Math.floor(Math.random()*keys.length)];
        if (b === a) b = keys[(keys.indexOf(a)+1) % keys.length];
        spawnArc(a, b);
    }
}

/* ═══════════════════════════════════════════════════════
   ANIMATION LOOP
═══════════════════════════════════════════════════════ */
let lastTime = 0;
function animate(now) {
    requestAnimationFrame(animate);
    const dt = now - lastTime;
    lastTime = now;
    if (dt > 200) return;   // tab was hidden

    if (autoRotate) spherical.theta += 0.003;
    updateCamera();

    brainGroup.updateMatrixWorld();
    decayLobes(dt);
    updateArcs(dt);
    tickIdleSparks(dt);
    updateLabels();

    // gentle pulsing on the key light
    keyLight.intensity = 1.2 + 0.15 * Math.sin(now * 0.001);

    renderer.render(scene, camera);
}
requestAnimationFrame(animate);

/* ═══════════════════════════════════════════════════════
   EVENT POLLING & STATE
═══════════════════════════════════════════════════════ */
let lastSeenTs = 0;
let msgCount = 0, msgWindowStart = Date.now(), busRate = 0;
let remCycles = 0;

function hexFromLobe(key) { return LOBES[key] ? LOBES[key].hex : '#888'; }

function addStreamEntry(srcKey, intent, dstKey) {
    const stream = document.getElementById('neural-stream');
    const entry = document.createElement('div');
    entry.className = 'stream-entry';
    const color = hexFromLobe(srcKey || 'frontal');
    const srcName = srcKey ? LOBES[srcKey].name : (intent || '?');
    const dstName = dstKey ? LOBES[dstKey].name : '?';
    const ts = new Date().toLocaleTimeString('en', {hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
    entry.innerHTML = `
        <div class="stream-dot" style="background:${color}"></div>
        <div class="stream-body">
            <div class="stream-route" style="color:${color}">${srcName} → ${dstName}</div>
            <div style="color:var(--text-dim);font-size:8px;">${intent}</div>
        </div>
        <div class="stream-time">${ts}</div>
    `;
    stream.prepend(entry);
    while (stream.children.length > 20) stream.removeChild(stream.lastChild);
}

function processEvent(ev) {
    msgCount++;
    const origin  = ev.origin  || '';
    const intent  = ev.intent  || '';
    const adapter = ev.adapter || '';

    const srcKey = ORIGIN_LOBE[origin] || null;

    let dstKey = null;
    if (intent === 'inference_result') {
        dstKey = adapter === 'critic' ? 'cingulate' : 'frontal';
    } else {
        dstKey = INTENT_TARGET[intent] || null;
    }

    if (srcKey) activateLobe(srcKey);
    if (dstKey) activateLobe(dstKey);
    if (srcKey && dstKey && srcKey !== dstKey) spawnArc(srcKey, dstKey);

    addStreamEntry(srcKey, intent, dstKey);

    /* Right panel updates */
    if (intent === 'inference_request') {
        const goalMatch = (ev.text||'').match(/GOAL:\s*(.*)/);
        if (goalMatch) document.getElementById('active-goal').textContent = goalMatch[1].trim();
    }
    if (intent === 'inference_result' && adapter !== 'critic') {
        try {
            const m = (ev.text||'').match(/"thought"\s*:\s*"([^"]+)"/);
            if (m) document.getElementById('last-thought').textContent = m[1];
        } catch(e) {}
    }
    if (intent === 'homeostatic_pulse') {
        const sr = Math.max(0, ev.success_rate || 0);
        const st = 1 - sr;
        document.getElementById('stat-success').textContent = (sr*100).toFixed(0)+'%';
        document.getElementById('stat-stress').textContent  = (st*100).toFixed(0)+'%';
        document.getElementById('stress-bar').style.width   = (st*100).toFixed(0)+'%';
        document.getElementById('stress-bar').style.background = st > 0.5 ? '#ef4444' : '#f59e0b';
    }
    if (intent === 'sleep_cycle_complete') {
        remCycles++;
        document.getElementById('stat-rem').textContent = remCycles;
    }

    // DREAM / SURGERY / REALITY mode
    if (intent.includes('dream') || origin.includes('rem')) {
        document.getElementById('hud-mode-val').textContent = 'DREAM';
    } else if (intent.includes('surgery') || intent.includes('patch')) {
        document.getElementById('hud-mode-val').textContent = 'SURGERY';
    } else {
        document.getElementById('hud-mode-val').textContent = 'REALITY';
    }

    // Ralph tasks
    if ((ev.cid||'').startsWith('ralph_') || (ev.task_id||'').startsWith('ralph_')) {
        updateRalphTask(ev.cid || ev.task_id, intent, ev.status);
    }
}

const ralphTaskMap = {};
function updateRalphTask(id, intent, status) {
    const container = document.getElementById('ralph-tasks');
    if (container.querySelector('div[style]')) container.innerHTML = '';
    if (!ralphTaskMap[id]) {
        const el = document.createElement('div');
        el.className = 'task-entry';
        el.id = 'task-' + id;
        el.textContent = id + ': ' + intent;
        container.prepend(el);
        ralphTaskMap[id] = el;
    }
    const el = ralphTaskMap[id];
    if (status === 'success') { el.classList.add('done'); el.textContent = id + ': done'; }
    if (status === 'fail'   ) { el.classList.add('fail'); el.textContent = id + ': FAILED'; }
}

/* Bus rate counter */
setInterval(() => {
    const now = Date.now();
    const elapsed = (now - msgWindowStart) / 1000;
    busRate = elapsed > 0 ? (msgCount / elapsed) : 0;
    document.getElementById('stat-bus').textContent    = busRate.toFixed(1);
    document.getElementById('hud-rate-val').textContent = busRate.toFixed(1);
    msgCount = 0; msgWindowStart = now;
}, 2000);

async function poll() {
    try {
        const res = await fetch('/events');
        if (!res.ok) return;
        const events = await res.json();
        if (!Array.isArray(events) || events.length === 0) return;

        const newEvs = events.filter(e => (e.ui_ts||0) > lastSeenTs);
        if (newEvs.length === 0) return;
        lastSeenTs = Math.max(...newEvs.map(e => e.ui_ts||0));

        // Process oldest-first
        newEvs.slice().reverse().forEach(processEvent);
    } catch(e) {}
}
setInterval(poll, 500);
</script>
</body>
</html>
)NEURO_HTML";
    }
};

} // namespace neuroswarm

int main(int argc, char** argv) {
    std::string ip = "localhost";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--thalamus" && i + 1 < argc) ip = argv[i+1];
    }
    neuroswarm::VisualizerLobe visualizer(ip);
    visualizer.start();
    return 0;
}
