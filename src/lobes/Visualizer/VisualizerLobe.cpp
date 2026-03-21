#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <common/routing.hpp>
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
        routing::subscribe_all(sub);

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
            auto j = routing::receive(sub);
            if (j.is_null()) continue;
            try {
                std::lock_guard<std::mutex> lock(event_mutex);
                j["ui_ts"] = std::time(nullptr);
                event_buffer.push_front(j);
                if (event_buffer.size() > max_events) event_buffer.pop_back();
            } catch (...) {}
        }
    }

    std::string get_dashboard_html() {
        return R"NEURO_HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>NeuroSwarm — Cognitive Architecture Dashboard</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=IBM+Plex+Mono:wght@400;500;600&display=swap');
:root {
    --bg: #f8fafc;
    --panel-bg: #ffffff;
    --panel-border: #e2e8f0;
    --canvas-bg: #f1f5f9;
    --text: #1e293b;
    --text-mid: #475569;
    --text-dim: #94a3b8;
    --accent: #2563eb;
    --success: #059669;
    --warning: #d97706;
    --danger: #dc2626;
    --font: 'Inter','Helvetica Neue',Arial,sans-serif;
    --mono: 'IBM Plex Mono','Menlo',monospace;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font);
    display: grid;
    grid-template-columns: 250px 1fr 290px;
    height: 100vh;
    overflow: hidden;
    -webkit-font-smoothing: antialiased;
}
::-webkit-scrollbar { width: 4px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: #cbd5e1; border-radius: 2px; }

/* LEFT PANEL */
#left-panel {
    background: var(--panel-bg);
    border-right: 1px solid var(--panel-border);
    display: flex; flex-direction: column;
    padding: 20px 16px; gap: 16px;
    overflow-y: auto;
}
.section-label {
    font-size: 10px; font-weight: 600; letter-spacing: 1.5px;
    color: var(--text-dim); text-transform: uppercase;
    margin-bottom: 8px;
}
#lobe-list { display: flex; flex-direction: column; gap: 2px; }
.lobe-row {
    display: flex; align-items: center; gap: 8px;
    padding: 6px 8px; border-radius: 6px;
    transition: background 0.2s;
    cursor: default;
}
.lobe-row:hover { background: #f1f5f9; }
.lobe-row.active { background: #eff6ff; }
.lobe-dot {
    width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0;
    border: 1.5px solid currentColor;
    transition: all 0.3s;
}
.lobe-dot.filled { background: currentColor; }
.lobe-dot.pulse { animation: dotPulse 0.5s ease-out; }
@keyframes dotPulse { 0%{transform:scale(1)} 50%{transform:scale(1.6)} 100%{transform:scale(1)} }
.lobe-info { flex: 1; min-width: 0; }
.lobe-name { font-size: 11px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; color: var(--text); }
.lobe-desc { font-size: 9px; color: var(--text-dim); font-weight: 400; }
.lobe-status { font-size: 9px; font-weight: 600; letter-spacing: 0.5px; }
.lobe-status.idle { color: var(--text-dim); }
.lobe-status.active { color: var(--accent); }

/* Drive indicator */
.drive-box {
    background: var(--bg); border: 1px solid var(--panel-border);
    border-radius: 8px; padding: 10px 12px; text-align: center;
    margin-bottom: 4px;
}
.drive-level {
    font-family: var(--mono); font-size: 13px; font-weight: 700;
    letter-spacing: 1.5px; color: var(--accent);
}
.drive-domain {
    font-size: 10px; color: var(--text-dim); margin-top: 2px;
}
/* Neurogenesis stats row */
.neuro-stat {
    flex: 1; text-align: center; padding: 6px 4px;
    background: var(--bg); border: 1px solid var(--panel-border);
    border-radius: 6px;
}
.neuro-val { font-family: var(--mono); font-size: 14px; font-weight: 600; color: var(--text); display: block; }
.neuro-lbl { font-size: 8px; color: var(--text-dim); text-transform: uppercase; letter-spacing: 0.5px; }

.stat-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.stat-box {
    background: var(--bg); border: 1px solid var(--panel-border);
    border-radius: 8px; padding: 10px; text-align: center;
}
.stat-val { font-family: var(--mono); font-size: 18px; font-weight: 600; color: var(--text); }
.stat-lbl { font-size: 9px; color: var(--text-dim); letter-spacing: 0.5px; text-transform: uppercase; margin-top: 2px; font-weight: 500; }
.bar-fill { border-radius: 2px; transition: width 0.5s, background 0.5s; }

/* CENTER */
#center { position: relative; overflow: hidden; background: var(--canvas-bg); }
#brain-canvas { display: block; width: 100%; height: 100%; }
#tooltip {
    position: absolute; pointer-events: none; z-index: 20;
    font-family: var(--font); font-size: 11px;
    padding: 8px 14px; border-radius: 8px;
    background: #ffffff; border: 1px solid var(--panel-border);
    box-shadow: 0 4px 12px rgba(0,0,0,0.08);
    color: var(--text); white-space: nowrap;
    display: none; transform: translate(-50%, -100%); margin-top: -14px;
}
#tooltip .tt-name { font-weight: 700; font-size: 12px; margin-bottom: 2px; }
#tooltip .tt-desc { color: var(--text-dim); font-size: 10px; font-weight: 400; }
#task-bar {
    position: absolute; bottom: 0; left: 0; right: 0;
    display: flex; align-items: center; gap: 0;
    background: rgba(255,255,255,0.90);
    backdrop-filter: blur(8px);
    border-top: 1px solid var(--panel-border);
    z-index: 8; font-size: 11px; color: var(--text-mid);
    overflow: hidden; height: 36px;
}
.tb-label {
    flex-shrink: 0; padding: 0 14px;
    font-size: 10px; font-weight: 600; letter-spacing: 1px;
    text-transform: uppercase; color: var(--text-dim);
    border-right: 1px solid var(--panel-border);
    height: 100%; display: flex; align-items: center;
}
#task-ticker {
    flex: 1; display: flex; align-items: center; gap: 0;
    overflow-x: auto; padding: 0 8px; height: 100%;
    scroll-behavior: smooth;
}
#task-ticker::-webkit-scrollbar { height: 0; }
.tick-item {
    flex-shrink: 0; display: flex; align-items: center; gap: 5px;
    padding: 4px 12px; font-family: var(--mono); font-size: 10px;
    border-right: 1px solid #f1f5f9; white-space: nowrap;
    animation: tickIn 0.3s ease-out;
}
@keyframes tickIn { from{opacity:0;transform:translateY(8px)} to{opacity:1;transform:none} }
.tick-item .tick-dot { width: 6px; height: 6px; border-radius: 50%; flex-shrink: 0; }
.tick-item.done { color: var(--success); }
.tick-item.done .tick-dot { background: var(--success); }
.tick-item.fail { color: var(--danger); }
.tick-item.fail .tick-dot { background: var(--danger); }
.tick-item.running { color: var(--accent); }
.tick-item.running .tick-dot { background: var(--accent); animation: sbPulse 1.5s infinite; }
@keyframes sbPulse { 0%,100%{opacity:1} 50%{opacity:0.3} }
.tb-brand {
    flex-shrink: 0; margin-left: auto; padding: 0 16px;
    font-size: 11px; font-weight: 600; letter-spacing: 1px;
    color: #cbd5e1;
    height: 100%; display: flex; align-items: center; gap: 6px;
    border-left: 1px solid var(--panel-border);
    user-select: none;
}
.tb-brand span { color: #94a3b8; font-weight: 400; font-size: 10px; letter-spacing: 0; }

/* RIGHT PANEL */
#right-panel {
    background: var(--panel-bg);
    border-left: 1px solid var(--panel-border);
    display: flex; flex-direction: column;
    padding: 20px 16px; gap: 16px;
    overflow-y: auto;
}
.r-section { display: flex; flex-direction: column; gap: 6px; }
.focus-text {
    font-size: 12px; color: var(--text); line-height: 1.5;
    padding: 10px 12px; background: #f0fdf4;
    border-left: 3px solid var(--success); border-radius: 4px;
    min-height: 36px; font-weight: 500;
}
.thought-text {
    font-size: 11px; color: var(--text-mid); font-style: italic; line-height: 1.5;
    padding: 10px 12px; background: var(--bg);
    border-left: 3px solid #e2e8f0; border-radius: 4px;
    min-height: 36px;
}
/* Pipeline Step Indicator */
#pipeline-steps {
    display: flex; align-items: center; gap: 0;
    padding: 8px 10px; background: var(--bg); border-radius: 6px;
    border: 1px solid var(--panel-border); overflow-x: auto;
}
.pipe-step {
    display: flex; align-items: center; gap: 0; white-space: nowrap;
}
.pipe-label {
    font-size: 9px; font-family: var(--mono); color: var(--text-dim);
    padding: 3px 6px; border-radius: 3px; transition: all 0.3s;
    text-transform: uppercase; letter-spacing: 0.5px;
}
.pipe-label.active {
    background: #2563eb; color: #fff; font-weight: 600;
    box-shadow: 0 0 8px rgba(37,99,235,0.3);
}
.pipe-label.done { color: var(--success); }
.pipe-arrow {
    font-size: 9px; color: var(--text-dim); margin: 0 2px;
}
#neural-stream { flex: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 2px; min-height: 0; }
.stream-entry {
    display: flex; align-items: flex-start; gap: 8px;
    padding: 5px 8px; border-radius: 4px;
    animation: streamIn 0.2s ease-out;
    font-size: 10px;
}
.stream-entry:hover { background: #f8fafc; }
@keyframes streamIn { from{opacity:0;transform:translateY(-4px)} to{opacity:1;transform:none} }
.stream-dot { width: 6px; height: 6px; border-radius: 50%; flex-shrink:0; margin-top:4px; }
.stream-body { flex:1; min-width:0; }
.stream-route { font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; color: var(--text); }
.stream-intent { color: var(--text-dim); font-size: 9px; font-family: var(--mono); }
.stream-time { color: var(--text-dim); font-size: 9px; flex-shrink:0; font-family: var(--mono); }
</style>
</head>
<body>

<!-- LEFT PANEL -->
<div id="left-panel">
    <div>
        <div class="section-label">Active Lobes</div>
        <div id="lobe-list"></div>
    </div>
    <div style="margin-top:auto;">
        <!-- Drive Level -->
        <div class="section-label">Active Drive</div>
        <div id="drive-indicator" class="drive-box">
            <div class="drive-level" id="drive-level">—</div>
            <div class="drive-domain" id="drive-domain">awaiting goal</div>
        </div>

        <!-- Core Metrics: 2x2 grid -->
        <div class="section-label" style="margin-top:14px;">System Metrics</div>
        <div class="stat-grid">
            <div class="stat-box">
                <div class="stat-val" id="stat-success" style="color:var(--success)">—</div>
                <div class="stat-lbl">Success Rate</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-stamina" style="color:var(--accent)">—</div>
                <div class="stat-lbl">Stamina</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-tasks">0</div>
                <div class="stat-lbl">Tasks Done</div>
            </div>
            <div class="stat-box">
                <div class="stat-val" id="stat-rem">0</div>
                <div class="stat-lbl">REM Cycles</div>
            </div>
        </div>

        <!-- Neurogenesis Stats -->
        <div style="margin-top:10px;display:flex;gap:8px;">
            <div class="neuro-stat">
                <span class="neuro-val" id="stat-specialists">0</span>
                <span class="neuro-lbl">Specialists</span>
            </div>
            <div class="neuro-stat">
                <span class="neuro-val" id="stat-genesis">0</span>
                <span class="neuro-lbl">Genesis</span>
            </div>
            <div class="neuro-stat">
                <span class="neuro-val" id="stat-apoptosis">0</span>
                <span class="neuro-lbl">Apoptosis</span>
            </div>
        </div>

        <!-- Learning Curve -->
        <div style="margin-top:14px;">
            <div class="section-label" style="margin-bottom:4px;">Learning Curve</div>
            <canvas id="learning-chart" width="240" height="80" style="width:100%;height:80px;border-radius:8px;background:var(--bg);border:1px solid var(--panel-border);"></canvas>
            <div style="display:flex;justify-content:space-between;font-size:9px;color:var(--text-dim);margin-top:3px;">
                <span id="lc-time-start">—</span>
                <span style="color:var(--success);font-weight:500;">success rate %</span>
                <span id="lc-time-end">now</span>
            </div>
        </div>
    </div>
</div>

<!-- CENTER: NETWORK GRAPH -->
<div id="center">
    <canvas id="brain-canvas"></canvas>
    <div id="tooltip"><div class="tt-name"></div><div class="tt-desc"></div></div>
    <div id="task-bar">
        <div class="tb-label">Spike Tasks</div>
        <div id="task-ticker"></div>
        <div class="tb-brand">NeuroSwarm <span>v2.2</span></div>
    </div>
</div>

<!-- RIGHT PANEL -->
<div id="right-panel">
    <div class="r-section">
        <div class="section-label">Cognitive Focus</div>
        <div class="focus-text" id="active-goal">Idle — awaiting directive...</div>
    </div>
    <div class="r-section">
        <div class="section-label">Cognitive Pipeline</div>
        <div id="pipeline-steps">
            <div class="pipe-step"><span class="pipe-label" id="pipe-goal">Goal</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-plan">Plan</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-suggest">Suggest</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-llm">LLM</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-critic">Critic</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-dream">Dream</span><span class="pipe-arrow">&rsaquo;</span></div>
            <div class="pipe-step"><span class="pipe-label" id="pipe-exec">Exec</span></div>
        </div>
    </div>
    <div class="r-section">
        <div class="section-label">Last Thought</div>
        <div class="thought-text" id="last-thought">Standby.</div>
    </div>
    <div class="r-section" style="flex:1;min-height:0;display:flex;flex-direction:column;">
        <div class="section-label">Event Stream</div>
        <div id="neural-stream"></div>
    </div>
</div>

<script>
'use strict';

/* ═══════════════════════════════════════════════════════
   NODE DEFINITIONS — All 16 core lobes
   Academic color palette — muted, professional
═══════════════════════════════════════════════════════ */
const NODES = {
    frontal:     { name:"Frontal Executive",  component:"frontal_executive",   color:"#2563eb", x:0.50, y:0.28, r:28, desc:"Planning & Goal Orchestration",   cluster:"executive" },
    synaptic:    { name:"Synaptic Cortex",    component:"synaptic_controller", color:"#7c3aed", x:0.30, y:0.18, r:24, desc:"LLM Inference Engine",            cluster:"inference" },
    metacog:     { name:"MetaCognition",      component:"metacognition",       color:"#8b5cf6", x:0.20, y:0.30, r:12, desc:"Self-Observation Layer",          cluster:"inference" },
    hippocampus: { name:"Hippocampus",        component:"hippocampus",         color:"#c2410c", x:0.16, y:0.48, r:20, desc:"Episodic & Semantic Memory",      cluster:"memory" },
    rem:         { name:"REM Engine",         component:"rem_engine",          color:"#d97706", x:0.10, y:0.60, r:15, desc:"Sleep & Consolidation",           cluster:"memory" },
    wernicke:    { name:"Wernicke Cortex",    component:"wernicke_lobe",       color:"#059669", x:0.73, y:0.18, r:15, desc:"Natural Language Understanding",  cluster:"language" },
    critic:      { name:"Critic Lobe",        component:"critic_lobe",         color:"#ea580c", x:0.76, y:0.34, r:17, desc:"Adversarial Safety Validation",   cluster:"safety" },
    amygdala:    { name:"Amygdala",           component:"amygdala",            color:"#dc2626", x:0.85, y:0.44, r:14, desc:"Threat Detection & Response",     cluster:"safety" },
    visual:      { name:"Visual Cortex",      component:"visual_lobe",         color:"#0891b2", x:0.12, y:0.38, r:14, desc:"Filesystem Monitoring (inotify)", cluster:"perception" },
    motor:       { name:"Motor Cortex",       component:"motor_cortex",        color:"#16a34a", x:0.80, y:0.56, r:21, desc:"Command Execution Engine",        cluster:"motor" },
    thalamus:    { name:"Thalamus",           component:"thalamus",            color:"#4f46e5", x:0.50, y:0.48, r:23, desc:"ZMQ XPUB/XSUB Message Relay",    cluster:"infra" },
    basal:       { name:"Basal Ganglia",      component:"basal_ganglia",       color:"#db2777", x:0.58, y:0.66, r:19, desc:"Drive Hierarchy & Neurogenesis",  cluster:"motivation" },
    homeostasis: { name:"Homeostasis",        component:"homeostasis",         color:"#e11d48", x:0.28, y:0.72, r:15, desc:"Autonomic Stress Regulation",     cluster:"autonomic" },
    chronos:     { name:"Chronos Lobe",       component:"chronos_lobe",        color:"#f43f5e", x:0.38, y:0.80, r:12, desc:"Circadian & Temporal Awareness",  cluster:"autonomic" },
    statistics:  { name:"Statistics",         component:"statistics_lobe",     color:"#6366f1", x:0.70, y:0.76, r:11, desc:"Performance Telemetry",           cluster:"infra" },
    concept:     { name:"Concept Space",     component:"concept_lobe",        color:"#8b5cf6", x:0.86, y:0.68, r:14, desc:"Internal Representations",        cluster:"inference" }
};

const EDGES = [
    {s:'frontal',t:'synaptic'},    {s:'frontal',t:'hippocampus'},
    {s:'frontal',t:'critic'},      {s:'frontal',t:'motor'},
    {s:'frontal',t:'thalamus'},    {s:'frontal',t:'basal'},
    {s:'wernicke',t:'frontal'},    {s:'visual',t:'frontal'},
    {s:'amygdala',t:'frontal'},    {s:'homeostasis',t:'frontal'},
    {s:'metacog',t:'frontal'},     {s:'homeostasis',t:'chronos'},
    {s:'rem',t:'hippocampus'},     {s:'rem',t:'synaptic'},
    {s:'basal',t:'motor'},         {s:'basal',t:'homeostasis'},
    {s:'metacog',t:'synaptic'},    {s:'statistics',t:'thalamus'},
    {s:'critic',t:'amygdala'},     {s:'thalamus',t:'motor'},
    {s:'thalamus',t:'synaptic'},   {s:'basal',t:'thalamus'},
    {s:'concept',t:'frontal'},     {s:'concept',t:'basal'}
];

const CLUSTERS = {
    executive:  { color:'#2563eb', nodes:['frontal'] },
    inference:  { color:'#7c3aed', nodes:['synaptic','metacog','concept'] },
    memory:     { color:'#c2410c', nodes:['hippocampus','rem'] },
    language:   { color:'#059669', nodes:['wernicke'] },
    safety:     { color:'#ea580c', nodes:['critic','amygdala'] },
    perception: { color:'#0891b2', nodes:['visual'] },
    motor:      { color:'#16a34a', nodes:['motor'] },
    infra:      { color:'#4f46e5', nodes:['thalamus','statistics'] },
    motivation: { color:'#db2777', nodes:['basal'] },
    autonomic:  { color:'#e11d48', nodes:['homeostasis','chronos'] }
};

const ORIGIN_NODE = {
    'frontal_executive':'frontal', 'synaptic_controller':'synaptic',
    'motor_cortex':'motor',        'critic_lobe':'critic',
    'hippocampus':'hippocampus',   'rem_engine':'rem',
    'homeostasis':'homeostasis',   'visual_lobe':'visual',
    'thalamus':'thalamus',         'wernicke_lobe':'wernicke',
    'metacognition':'metacog',     'amygdala':'amygdala',
    'basal_ganglia':'basal',       'chronos_lobe':'chronos',
    'statistics_lobe':'statistics','concept_lobe':'concept',
    'spike_worker':'frontal',      'cerebral_matrix':'thalamus',
    'broca_terminal':'wernicke',   'user_terminal':'wernicke'
};
const INTENT_TARGET = {
    'inference_request':'synaptic',  'inference_result':'frontal',
    'execution_request':'motor',     'execution_result':'frontal',
    'critic_validate':'critic',      'critic_result':'frontal',
    'search_memory':'hippocampus',   'search_result':'frontal',
    'embedding_request':'hippocampus','homeostatic_pulse':'homeostasis',
    'visual_stimulus':'frontal',     'stimulus':'frontal',
    'intrinsic_goal':'frontal',      'intrinsic_goal_request':'basal',
    'genesis_request':'motor',       'genesis_result':'basal',
    'inject_lobe':'thalamus',        'lobe_injected':'thalamus',
    'lobe_terminated':'thalamus',    'lobe_crash':'thalamus',
    'sleep_cycle_complete':'rem',    'spike_assign':'motor',
    'spike_ready':'frontal',         'spike_done':'frontal',
    'specialist_advice':'motor',     'specialist_report':'basal',
    'threat_assessment':'amygdala',  'metacognitive_report':'metacog',
    'concept_query':'concept',        'concept_response':'frontal',
    'concept_update':'concept',       'concept_transfer':'frontal'
};

/* ═══════════════════════════════════════════════════════
   BUILD LEFT PANEL LOBE LIST
═══════════════════════════════════════════════════════ */
const lobeListEl = document.getElementById('lobe-list');
Object.entries(NODES).forEach(([key, N]) => {
    const row = document.createElement('div');
    row.className = 'lobe-row';
    row.id = 'lobe-row-' + key;
    row.innerHTML = `
        <div class="lobe-dot" id="dot-${key}" style="color:${N.color};"></div>
        <div class="lobe-info">
            <div class="lobe-name">${N.name}</div>
            <div class="lobe-desc">${N.desc}</div>
        </div>
        <div class="lobe-status idle" id="status-${key}">IDLE</div>
    `;
    lobeListEl.appendChild(row);
});

/* ═══════════════════════════════════════════════════════
   CANVAS SETUP
═══════════════════════════════════════════════════════ */
const canvas = document.getElementById('brain-canvas');
const ctx2 = canvas.getContext('2d');
const centerEl = document.getElementById('center');
const tooltip = document.getElementById('tooltip');
let W = 0, H = 0;

function resize() {
    W = centerEl.clientWidth;
    H = centerEl.clientHeight;
    canvas.width = W * devicePixelRatio;
    canvas.height = H * devicePixelRatio;
    canvas.style.width = W + 'px';
    canvas.style.height = H + 'px';
    ctx2.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
}
resize();
window.addEventListener('resize', resize);

/* ═══════════════════════════════════════════════════════
   STATE
═══════════════════════════════════════════════════════ */
const activity = {};
const edgeTraffic = {};
const particles = [];
let hoveredNode = null;
let dynamicNodes = {};
let specialistCount = 0;

Object.keys(NODES).forEach(k => {
    activity[k] = { intensity: 0, phase: Math.random() * Math.PI * 2 };
});
EDGES.forEach(e => {
    edgeTraffic[e.s+'|'+e.t] = { intensity: 0, total: 0 };
    edgeTraffic[e.t+'|'+e.s] = { intensity: 0, total: 0 };
});

/* ═══════════════════════════════════════════════════════
   COLOR UTILITIES
═══════════════════════════════════════════════════════ */
function hexToRGB(hex) {
    return { r:parseInt(hex.slice(1,3),16), g:parseInt(hex.slice(3,5),16), b:parseInt(hex.slice(5,7),16) };
}
function rgba(hex, a) {
    const {r,g,b} = hexToRGB(hex);
    return 'rgba('+r+','+g+','+b+','+a+')';
}

/* ═══════════════════════════════════════════════════════
   POSITION HELPERS
═══════════════════════════════════════════════════════ */
function nx(node) {
    const n = NODES[node] || dynamicNodes[node];
    return n ? n.x * W : 0;
}
function ny(node) {
    const n = NODES[node] || dynamicNodes[node];
    return n ? n.y * H : 0;
}

/* ═══════════════════════════════════════════════════════
   EDGE GEOMETRY
═══════════════════════════════════════════════════════ */
function edgeCP(x1,y1,x2,y2,idx) {
    const mx=(x1+x2)/2, my=(y1+y2)/2;
    const dx=x2-x1, dy=y2-y1;
    const len = Math.sqrt(dx*dx+dy*dy) || 1;
    const dir = (idx%2===0) ? 1 : -1;
    const off = Math.min(len * 0.15, 50) * dir;
    return { x: mx + (-dy/len)*off, y: my + (dx/len)*off };
}
function quadBez(t,x0,y0,cx,cy,x1,y1) {
    const u=1-t;
    return { x: u*u*x0+2*u*t*cx+t*t*x1, y: u*u*y0+2*u*t*cy+t*t*y1 };
}

/* ═══════════════════════════════════════════════════════
   RENDERING — Clean Academic Style
═══════════════════════════════════════════════════════ */

function drawDensityClouds() {
    Object.values(CLUSTERS).forEach(cl => {
        if (!cl.nodes.length) return;
        let cx=0, cy=0;
        cl.nodes.forEach(k => { cx += nx(k); cy += ny(k); });
        cx /= cl.nodes.length; cy /= cl.nodes.length;

        let spread = 70;
        cl.nodes.forEach(k => {
            const dx=nx(k)-cx, dy=ny(k)-cy;
            spread = Math.max(spread, Math.sqrt(dx*dx+dy*dy) + 50);
        });
        const cloudR = spread * 2.0;

        const grad = ctx2.createRadialGradient(cx,cy,0,cx,cy,cloudR);
        grad.addColorStop(0, rgba(cl.color, 0.06));
        grad.addColorStop(0.5, rgba(cl.color, 0.025));
        grad.addColorStop(1, rgba(cl.color, 0));
        ctx2.fillStyle = grad;
        ctx2.beginPath();
        ctx2.arc(cx,cy,cloudR,0,Math.PI*2);
        ctx2.fill();
    });
}

function drawEdges() {
    const allEdges = [...EDGES];
    Object.keys(dynamicNodes).forEach(dk => {
        allEdges.push({s:'basal',t:dk});
        allEdges.push({s:'motor',t:dk});
    });

    allEdges.forEach((e,i) => {
        const n1 = NODES[e.s] || dynamicNodes[e.s];
        const n2 = NODES[e.t] || dynamicNodes[e.t];
        if (!n1 || !n2) return;

        const x1=nx(e.s), y1=ny(e.s), x2=nx(e.t), y2=ny(e.t);
        const cp = edgeCP(x1,y1,x2,y2,i);

        const key = e.s+'|'+e.t;
        const traffic = edgeTraffic[key] || {intensity:0, total:0};
        const isHov = hoveredNode && (e.s===hoveredNode || e.t===hoveredNode);

        let alpha = 0.08 + Math.min(0.1, traffic.total * 0.002) + traffic.intensity * 0.25;
        let width = 1 + traffic.intensity * 1.2;
        let color = '#94a3b8';

        if (isHov) { alpha = 0.4; width = 2; color = n1.color; }
        if (traffic.intensity > 0.1) color = n1.color;

        ctx2.beginPath();
        ctx2.moveTo(x1,y1);
        ctx2.quadraticCurveTo(cp.x,cp.y,x2,y2);
        ctx2.strokeStyle = rgba(color, alpha);
        ctx2.lineWidth = width;
        ctx2.stroke();
    });
}

function drawParticles() {
    particles.forEach(p => {
        const t = p.t / p.duration;
        const pos = quadBez(t, p.sx,p.sy, p.cpx,p.cpy, p.tx,p.ty);

        // Subtle trail
        for (let i = 1; i <= 3; i++) {
            const tt = Math.max(0, t - i * 0.05);
            const tp = quadBez(tt, p.sx,p.sy, p.cpx,p.cpy, p.tx,p.ty);
            ctx2.beginPath();
            ctx2.arc(tp.x, tp.y, 3 - i*0.6, 0, Math.PI*2);
            ctx2.fillStyle = rgba(p.color, 0.15 - i*0.04);
            ctx2.fill();
        }

        // Head dot
        ctx2.beginPath();
        ctx2.arc(pos.x, pos.y, 3.5, 0, Math.PI*2);
        ctx2.fillStyle = rgba(p.color, 0.7);
        ctx2.fill();
    });
}

function drawNodes() {
    const allNodes = {...NODES, ...dynamicNodes};

    ctx2.save();
    Object.entries(allNodes).forEach(([key, N]) => {
        const x=nx(key), y=ny(key);
        const act = (activity[key] || {intensity:0}).intensity;
        const r = N.r * (1 + act * 0.08);
        const isHov = (key === hoveredNode);

        // Drop shadow
        ctx2.shadowColor = rgba(N.color, isHov ? 0.25 : 0.1 + act * 0.15);
        ctx2.shadowBlur = isHov ? 16 : (6 + act * 10);
        ctx2.shadowOffsetX = 0;
        ctx2.shadowOffsetY = 2;

        // Node body — solid fill with subtle gradient for depth
        const bodyGrad = ctx2.createRadialGradient(x - r*0.2, y - r*0.2, 0, x, y, r);
        const {r:cr,g:cg,b:cb} = hexToRGB(N.color);
        const lighten = isHov ? 40 : 20;
        bodyGrad.addColorStop(0, `rgba(${Math.min(255,cr+lighten)},${Math.min(255,cg+lighten)},${Math.min(255,cb+lighten)},0.92)`);
        bodyGrad.addColorStop(1, rgba(N.color, 0.85));
        ctx2.fillStyle = bodyGrad;
        ctx2.beginPath();
        ctx2.arc(x, y, r, 0, Math.PI*2);
        ctx2.fill();

        // Clean border
        ctx2.shadowColor = 'transparent';
        ctx2.shadowBlur = 0;
        ctx2.shadowOffsetY = 0;
        ctx2.strokeStyle = rgba(N.color, isHov ? 0.9 : 0.5);
        ctx2.lineWidth = isHov ? 2 : 1.5;
        ctx2.stroke();

        // Activity ring
        if (act > 0.1) {
            ctx2.beginPath();
            ctx2.arc(x, y, r + 4, 0, Math.PI*2);
            ctx2.strokeStyle = rgba(N.color, act * 0.3);
            ctx2.lineWidth = 1.5;
            ctx2.stroke();
        }
    });
    ctx2.restore();
}

function drawLabels() {
    const allNodes = {...NODES, ...dynamicNodes};
    ctx2.textAlign = 'center';
    ctx2.textBaseline = 'top';

    Object.entries(allNodes).forEach(([key, N]) => {
        const x=nx(key), y=ny(key);
        const act = (activity[key] || {intensity:0}).intensity;
        const isHov = (key === hoveredNode);

        const fontSize = Math.max(8, Math.min(11, N.r * 0.38));
        ctx2.font = `600 ${fontSize}px 'Inter',sans-serif`;

        const alpha = isHov ? 0.95 : (0.45 + act * 0.5);
        ctx2.fillStyle = rgba('#1e293b', alpha);
        ctx2.fillText(N.name, x, y + N.r + 6);
    });
}

/* ═══════════════════════════════════════════════════════
   MAIN RENDER
═══════════════════════════════════════════════════════ */
function render(time) {
    ctx2.clearRect(0,0,W,H);

    // Clean light background
    ctx2.fillStyle = '#f1f5f9';
    ctx2.fillRect(0,0,W,H);

    // Subtle grid pattern
    ctx2.strokeStyle = 'rgba(0,0,0,0.03)';
    ctx2.lineWidth = 0.5;
    const gridSize = 40;
    for (let x = gridSize; x < W; x += gridSize) {
        ctx2.beginPath(); ctx2.moveTo(x,0); ctx2.lineTo(x,H); ctx2.stroke();
    }
    for (let y = gridSize; y < H; y += gridSize) {
        ctx2.beginPath(); ctx2.moveTo(0,y); ctx2.lineTo(W,y); ctx2.stroke();
    }

    drawDensityClouds();
    drawEdges();
    drawParticles();
    drawNodes();
    drawLabels();
}

/* ═══════════════════════════════════════════════════════
   ACTIVATION & PARTICLES
═══════════════════════════════════════════════════════ */
function activateNode(key) {
    if (!activity[key]) activity[key] = { intensity: 0, phase: Math.random()*Math.PI*2 };
    activity[key].intensity = 1.0;

    const row = document.getElementById('lobe-row-' + key);
    const dot = document.getElementById('dot-' + key);
    const status = document.getElementById('status-' + key);
    if (row) row.classList.add('active');
    if (dot) { dot.classList.add('filled'); dot.classList.remove('pulse'); void dot.offsetWidth; dot.classList.add('pulse'); }
    if (status) { status.textContent = 'ACTIVE'; status.className = 'lobe-status active'; }
}

function spawnParticle(srcKey, dstKey) {
    const n1 = NODES[srcKey] || dynamicNodes[srcKey];
    const n2 = NODES[dstKey] || dynamicNodes[dstKey];
    if (!n1 || !n2) return;

    const x1=nx(srcKey), y1=ny(srcKey), x2=nx(dstKey), y2=ny(dstKey);
    const cp = edgeCP(x1,y1,x2,y2,particles.length);

    particles.push({
        sx:x1, sy:y1, tx:x2, ty:y2, cpx:cp.x, cpy:cp.y,
        t:0, duration:600+Math.random()*200,
        color: n1.color
    });

    const eKey = srcKey+'|'+dstKey;
    if (!edgeTraffic[eKey]) edgeTraffic[eKey] = {intensity:0, total:0};
    edgeTraffic[eKey].intensity = 1.0;
    edgeTraffic[eKey].total++;
}

/* ═══════════════════════════════════════════════════════
   ANIMATION LOOP
═══════════════════════════════════════════════════════ */
let lastTime = 0;
function animate(now) {
    requestAnimationFrame(animate);
    const dt = now - lastTime;
    lastTime = now;
    if (dt > 200) return;

    // Decay activity
    Object.entries(activity).forEach(([k,a]) => {
        if (a.intensity > 0) {
            a.intensity = Math.max(0, a.intensity - dt / 2500);
            if (a.intensity <= 0) {
                const row = document.getElementById('lobe-row-' + k);
                const dot = document.getElementById('dot-' + k);
                const status = document.getElementById('status-' + k);
                if (row) row.classList.remove('active');
                if (dot) dot.classList.remove('filled');
                if (status) { status.textContent = 'IDLE'; status.className = 'lobe-status idle'; }
            }
        }
    });

    // Decay edge traffic
    Object.values(edgeTraffic).forEach(et => {
        if (et.intensity > 0) et.intensity = Math.max(0, et.intensity - dt / 2000);
    });

    // Update particles
    for (let i = particles.length - 1; i >= 0; i--) {
        particles[i].t += dt;
        if (particles[i].t >= particles[i].duration) particles.splice(i,1);
    }

    render(now);
}
requestAnimationFrame(animate);

/* ═══════════════════════════════════════════════════════
   MOUSE INTERACTION
═══════════════════════════════════════════════════════ */
canvas.addEventListener('mousemove', e => {
    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left, my = e.clientY - rect.top;

    let found = null;
    const allNodes = {...NODES, ...dynamicNodes};
    Object.entries(allNodes).forEach(([key, N]) => {
        const dx = mx - nx(key), dy = my - ny(key);
        if (dx*dx+dy*dy < (N.r+6)*(N.r+6)) found = key;
    });

    hoveredNode = found;
    if (found) {
        const N = allNodes[found];
        tooltip.style.display = 'block';
        tooltip.style.left = mx + 'px';
        tooltip.style.top = my + 'px';
        tooltip.querySelector('.tt-name').textContent = N.name;
        tooltip.querySelector('.tt-name').style.color = N.color;
        tooltip.querySelector('.tt-desc').textContent = N.desc;
        canvas.style.cursor = 'pointer';
    } else {
        tooltip.style.display = 'none';
        canvas.style.cursor = 'default';
    }
});
canvas.addEventListener('mouseleave', () => {
    hoveredNode = null;
    tooltip.style.display = 'none';
});

/* ═══════════════════════════════════════════════════════
   IDLE SPARKS — Occasional ambient activity
═══════════════════════════════════════════════════════ */
setInterval(() => {
    const keys = Object.keys(NODES);
    const a = keys[Math.floor(Math.random()*keys.length)];
    let b = keys[Math.floor(Math.random()*keys.length)];
    if (b === a) b = keys[(keys.indexOf(a)+1)%keys.length];
    if (EDGES.some(e => (e.s===a&&e.t===b)||(e.s===b&&e.t===a))) spawnParticle(a, b);
}, 5000);

/* ═══════════════════════════════════════════════════════
   DYNAMIC SPECIALIST NODES
═══════════════════════════════════════════════════════ */
function addSpecialistNode(name) {
    const key = name.toLowerCase();
    if (NODES[key] || dynamicNodes[key]) return;

    specialistCount++;
    const angle = specialistCount * 1.2;
    const dist = 0.07 + specialistCount * 0.025;
    const bx = NODES.basal.x, by = NODES.basal.y;

    dynamicNodes[key] = {
        name: name.replace(/_/g,' '),
        component: name.toLowerCase(),
        color: '#a855f7',
        x: Math.min(0.92, Math.max(0.08, bx + Math.cos(angle) * dist)),
        y: Math.min(0.92, Math.max(0.08, by + Math.sin(angle) * dist)),
        r: 10, desc: 'Generated Specialist Lobe', cluster: 'motivation'
    };
    activity[key] = { intensity: 1.0, phase: Math.random()*Math.PI*2 };

    const row = document.createElement('div');
    row.className = 'lobe-row active';
    row.id = 'lobe-row-' + key;
    row.innerHTML = `
        <div class="lobe-dot filled pulse" id="dot-${key}" style="color:#a855f7;"></div>
        <div class="lobe-info">
            <div class="lobe-name">${dynamicNodes[key].name}</div>
            <div class="lobe-desc">Specialist</div>
        </div>
        <div class="lobe-status active" id="status-${key}">ACTIVE</div>
    `;
    lobeListEl.appendChild(row);
    if (CLUSTERS.motivation) CLUSTERS.motivation.nodes.push(key);
}

function removeSpecialistNode(name) {
    const key = name.toLowerCase();
    if (!dynamicNodes[key]) return;
    delete dynamicNodes[key]; delete activity[key];
    const row = document.getElementById('lobe-row-' + key);
    if (row) row.remove();
    const idx = CLUSTERS.motivation.nodes.indexOf(key);
    if (idx >= 0) CLUSTERS.motivation.nodes.splice(idx,1);
}

/* ═══════════════════════════════════════════════════════
   COGNITIVE PIPELINE TRACKER
═══════════════════════════════════════════════════════ */
const PIPE_IDS = ['pipe-goal','pipe-plan','pipe-suggest','pipe-llm','pipe-critic','pipe-dream','pipe-exec'];
let currentPipeStep = -1;

function setPipelineStep(stepIdx) {
    PIPE_IDS.forEach((id, i) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.classList.remove('active','done');
        if (i < stepIdx) el.classList.add('done');
        else if (i === stepIdx) el.classList.add('active');
    });
    currentPipeStep = stepIdx;
}

function updatePipeline(intent, ev) {
    if (intent === 'intrinsic_goal') setPipelineStep(0);
    else if (intent === 'goal_plan') setPipelineStep(1);
    else if (intent === 'execution_request' && (ev.source === 'suggested' || ev.source === 'basal_ganglia' || ev.tier === 'suggested')) setPipelineStep(2);
    else if (intent === 'inference_request' && (ev.adapter||'default') !== 'critic') setPipelineStep(3);
    else if (intent === 'critic_validate') setPipelineStep(4);
    else if (intent === 'execution_request' && ev.mode === 'dream') setPipelineStep(5);
    else if (intent === 'execution_request' && ev.mode === 'reality') setPipelineStep(6);
    else if (intent === 'execution_result' && ev.mode === 'reality') setPipelineStep(6);
    else if (intent === 'cognitive_idle') setPipelineStep(-1);
}

/* ═══════════════════════════════════════════════════════
   EVENT POLLING & PROCESSING
═══════════════════════════════════════════════════════ */
let lastSeenTs = 0;
let msgCount = 0, msgWindowStart = Date.now(), busRate = 0;
let remCycles = 0;
let tasksDone = 0;
let genesisCount = 0;
let apoptosisCount = 0;
let activeSpecialists = 0;
const LC_MAX = 120;
const lcData = [];

function hexFromNode(key) { return (NODES[key]||dynamicNodes[key]||{}).color || '#94a3b8'; }

function addStreamEntry(srcKey, intent, dstKey) {
    const stream = document.getElementById('neural-stream');
    const entry = document.createElement('div');
    entry.className = 'stream-entry';
    const allN = {...NODES, ...dynamicNodes};
    const color = hexFromNode(srcKey || 'frontal');
    const srcName = srcKey && allN[srcKey] ? allN[srcKey].name : '—';
    const dstName = dstKey && allN[dstKey] ? allN[dstKey].name : '—';
    const ts = new Date().toLocaleTimeString('en',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
    entry.innerHTML = `
        <div class="stream-dot" style="background:${color}"></div>
        <div class="stream-body">
            <div class="stream-route">${srcName} → ${dstName}</div>
            <div class="stream-intent">${intent}</div>
        </div>
        <div class="stream-time">${ts}</div>
    `;
    stream.prepend(entry);
    while (stream.children.length > 25) stream.removeChild(stream.lastChild);
}

function processEvent(ev) {
    msgCount++;
    const origin = ev.origin || '', intent = ev.intent || '', adapter = ev.adapter || '';

    const srcKey = ORIGIN_NODE[origin] || null;
    let dstKey = null;
    if (intent === 'inference_result') {
        dstKey = adapter === 'critic' ? 'critic' : 'frontal';
    } else {
        dstKey = INTENT_TARGET[intent] || null;
    }

    if (srcKey) activateNode(srcKey);
    if (dstKey) activateNode(dstKey);
    if (srcKey && dstKey && srcKey !== dstKey) spawnParticle(srcKey, dstKey);

    addStreamEntry(srcKey, intent, dstKey);
    updatePipeline(intent, ev);

    if (intent === 'inference_request') {
        const m = (ev.text||'').match(/GOAL:\s*(.*)/);
        if (m) document.getElementById('active-goal').textContent = m[1].trim();
    }
    if (intent === 'cognitive_idle') {
        document.getElementById('active-goal').textContent = 'Idle \u2014 awaiting directive...';
        document.getElementById('last-thought').textContent = 'Standby.';
    }
    if (intent === 'inference_result' && adapter !== 'critic') {
        try {
            const m = (ev.text||'').match(/"thought"\s*:\s*"([^"]+)"/);
            if (m) document.getElementById('last-thought').textContent = m[1];
        } catch(e){}
    }
    if (intent === 'homeostatic_pulse') {
        const sr = Math.max(0, ev.success_rate || 0);
        const stamina = ev.stamina != null ? ev.stamina : -1;
        document.getElementById('stat-success').textContent = (sr*100).toFixed(0)+'%';

        // Stamina
        if (stamina >= 0) {
            document.getElementById('stat-stamina').textContent = stamina.toFixed(0)+'%';
            const stColor = stamina < 30 ? 'var(--danger)' : stamina < 60 ? 'var(--warning)' : 'var(--accent)';
            document.getElementById('stat-stamina').style.color = stColor;
        }

        lcData.push({t:Date.now(), sr:sr});
        while (lcData.length > LC_MAX) lcData.shift();
        drawLearningCurve();
    }

    // Drive level from BasalGanglia
    if (intent === 'intrinsic_goal') {
        const drive = ev.drive_level || ev.context && ev.context.match(/Drive: (\w+)/) && RegExp.$1 || '';
        const domain = ev.domain || '';
        if (drive) {
            const driveEl = document.getElementById('drive-level');
            driveEl.textContent = drive;
            const driveColors = {
                'SURVIVAL':'#dc2626','HOMEOSTASIS':'#d97706',
                'EXPLORATION':'#2563eb','MASTERY':'#7c3aed','SELF_MODIFY':'#db2777'
            };
            driveEl.style.color = driveColors[drive] || 'var(--accent)';
            document.getElementById('drive-indicator').style.borderColor = (driveColors[drive] || 'var(--panel-border)') + '33';
        }
        if (domain) {
            document.getElementById('drive-domain').textContent = domain.replace(/_/g,' ');
        }
    }

    if (intent === 'sleep_cycle_complete') {
        remCycles++;
        document.getElementById('stat-rem').textContent = remCycles;
    }

    // Track task completions
    if (intent === 'execution_result') {
        tasksDone++;
        document.getElementById('stat-tasks').textContent = tasksDone;
    }

    // Neurogenesis / Apoptosis tracking
    if (intent === 'lobe_injected' && ev.lobe_name) {
        addSpecialistNode(ev.lobe_name);
        genesisCount++;
        activeSpecialists++;
        document.getElementById('stat-genesis').textContent = genesisCount;
        document.getElementById('stat-specialists').textContent = activeSpecialists;
    }
    if (intent === 'lobe_terminated' && ev.lobe_name) {
        removeSpecialistNode(ev.lobe_name);
        apoptosisCount++;
        activeSpecialists = Math.max(0, activeSpecialists - 1);
        document.getElementById('stat-apoptosis').textContent = apoptosisCount;
        document.getElementById('stat-specialists').textContent = activeSpecialists;
    }

    if ((ev.cid||'').startsWith('intrinsic_') || intent === 'spike_done' || intent === 'spike_assign') {
        const domain = (ev.text||'').match(/'([^']+)' domain/) ? RegExp.$1 : (ev.domain||intent);
        updateSpikeTask(ev.cid || ev.task_id || '', intent, ev.status, domain);
    }
}

const spikeTaskMap = {};
function updateSpikeTask(id, intent, status, domain) {
    if (!id) return;
    const ticker = document.getElementById('task-ticker');
    const shortId = id.length > 20 ? id.slice(-8) : id;
    const label = domain || intent || shortId;

    if (!spikeTaskMap[id]) {
        const el = document.createElement('div');
        el.className = 'tick-item running';
        el.innerHTML = '<div class="tick-dot"></div>' + label;
        el.id = 'tick-' + id.replace(/[^a-z0-9]/gi,'_');
        ticker.appendChild(el);
        spikeTaskMap[id] = el;
        // Keep max 30 items, remove oldest
        while (ticker.children.length > 30) ticker.removeChild(ticker.firstChild);
        // Auto-scroll to latest
        ticker.scrollLeft = ticker.scrollWidth;
    }
    const el = spikeTaskMap[id];
    if (status === 'success') {
        el.className = 'tick-item done';
        el.innerHTML = '<div class="tick-dot"></div>' + label + ' ✓';
    }
    if (status === 'failure' || status === 'fail') {
        el.className = 'tick-item fail';
        el.innerHTML = '<div class="tick-dot"></div>' + label + ' ✗';
    }
}

/* ═══════════════════════════════════════════════════════
   LEARNING CURVE
═══════════════════════════════════════════════════════ */
function drawLearningCurve() {
    const cvs = document.getElementById('learning-chart');
    if (!cvs || lcData.length < 2) return;
    const c = cvs.getContext('2d');
    const CW = cvs.width, CH = cvs.height;
    const pad = {t:8,b:14,l:4,r:4};
    const gW = CW-pad.l-pad.r, gH = CH-pad.t-pad.b;
    c.clearRect(0,0,CW,CH);

    c.strokeStyle = '#e2e8f0'; c.lineWidth = 0.5;
    [0.25,0.5,0.75].forEach(v => {
        const y = pad.t + gH*(1-v);
        c.beginPath(); c.moveTo(pad.l,y); c.lineTo(pad.l+gW,y); c.stroke();
    });

    const grad = c.createLinearGradient(0,pad.t,0,pad.t+gH);
    grad.addColorStop(0,'rgba(5,150,105,0.15)');
    grad.addColorStop(1,'rgba(5,150,105,0.02)');
    c.beginPath(); c.moveTo(pad.l,pad.t+gH);
    for (let i=0;i<lcData.length;i++) {
        c.lineTo(pad.l+(i/(LC_MAX-1))*gW, pad.t+gH*(1-lcData[i].sr));
    }
    c.lineTo(pad.l+((lcData.length-1)/(LC_MAX-1))*gW, pad.t+gH);
    c.closePath(); c.fillStyle = grad; c.fill();

    c.beginPath();
    for (let i=0;i<lcData.length;i++) {
        const x = pad.l+(i/(LC_MAX-1))*gW, y = pad.t+gH*(1-lcData[i].sr);
        i===0 ? c.moveTo(x,y) : c.lineTo(x,y);
    }
    c.strokeStyle = '#059669'; c.lineWidth = 1.5; c.stroke();

    if (lcData.length > 0) {
        const lx = pad.l+((lcData.length-1)/(LC_MAX-1))*gW;
        const ly = pad.t+gH*(1-lcData[lcData.length-1].sr);
        c.beginPath(); c.arc(lx,ly,3,0,Math.PI*2);
        c.fillStyle = '#059669'; c.fill();
        c.strokeStyle = '#ffffff'; c.lineWidth = 1.5; c.stroke();
    }
    if (lcData.length > 1) {
        const ago = Math.round((Date.now()-lcData[0].t)/1000);
        document.getElementById('lc-time-start').textContent = ago>60 ? Math.round(ago/60)+'m ago' : ago+'s ago';
    }
}

/* Bus rate */
setInterval(() => {
    const now = Date.now();
    const elapsed = (now-msgWindowStart)/1000;
    busRate = elapsed > 0 ? (msgCount/elapsed) : 0;
    msgCount = 0; msgWindowStart = now;
}, 2000);

/* Polling */
async function poll() {
    try {
        const res = await fetch('/events');
        if (!res.ok) return;
        const events = await res.json();
        if (!Array.isArray(events) || events.length === 0) return;
        const newEvs = events.filter(e => (e.ui_ts||0) > lastSeenTs);
        if (newEvs.length === 0) return;
        lastSeenTs = Math.max(...newEvs.map(e => e.ui_ts||0));
        newEvs.slice().reverse().forEach(processEvent);
    } catch(e){}
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
