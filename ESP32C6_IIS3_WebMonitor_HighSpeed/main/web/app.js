// IIS3DWB WebSocket Monitor - Main Application
(() => {
    let isPaused = false;
    let currentFullScale = 0;
    
    // DOM elements
    const statusEl = document.getElementById('status');
    const logEl = document.getElementById('log');
    const btnPause = document.getElementById('btnPause');
    const metrics = {
        msg: document.getElementById('metricMsg'),
        samples: document.getElementById('metricSamples'),
        mag: document.getElementById('metricMag')
    };

    // Pause/Resume control
    window.togglePause = () => {
        isPaused = !isPaused;
        fetch('/api/config', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({pause: isPaused})
        })
        .then(r => r.json())
        .then(d => {
            addLog(isPaused ? 'Streaming PAUSED' : 'Streaming RESUMED');
            btnPause.textContent = isPaused ? 'Resume' : 'Pause';
            btnPause.className = isPaused ? 'paused' : '';
            statusEl.textContent = isPaused ? 'Paused' : 'Connected';
        })
        .catch(e => addLog('Pause error: ' + e.message));
    };

    // Full scale control
    window.setFullScale = (fs, chartScale) => {
        fetch('/api/config', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({full_scale: fs})
        })
        .then(r => r.json())
        .then(d => {
            if (d.full_scale !== undefined) {
                currentFullScale = d.full_scale;
                const labels = ['±2g', '±4g', '±8g', '±16g'];
                addLog('Sensor: ' + labels[fs] + ', Chart: ' + (chartScale === null ? 'Auto' : '±' + chartScale + 'g'));
                
                // Update button states
                for (let i = 0; i < 4; i++) {
                    document.getElementById('fs' + i).className = (i === fs && chartScale !== null) ? 'active' : '';
                }
                document.getElementById('fsAuto').className = (chartScale === null) ? 'active' : '';
                
                // Update chart scale
                if (chartScale === null) {
                    chart.options.scales.y.min = undefined;
                    chart.options.scales.y.max = undefined;
                } else {
                    chart.options.scales.y.min = -chartScale;
                    chart.options.scales.y.max = chartScale;
                }
                chart.update();
            } else if (d.error) {
                addLog('FS error: ' + d.error);
            }
        })
        .catch(e => addLog('FS error: ' + e.message));
    };

    // Chart setup
    const ctx = document.getElementById('chartG').getContext('2d');
    const maxPoints = 200;
    const chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {label: 'X', data: [], borderColor: '#ef4444', borderWidth: 1.5, pointRadius: 0},
                {label: 'Y', data: [], borderColor: '#10b981', borderWidth: 1.5, pointRadius: 0},
                {label: 'Z', data: [], borderColor: '#3b82f6', borderWidth: 1.5, pointRadius: 0}
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            scales: {
                x: {display: false},
                y: {beginAtZero: false, grid: {color: 'rgba(226,232,240,0.8)'}}
            },
            plugins: {
                legend: {labels: {color: '#1e293b'}}
            }
        }
    });

    let sampleIndex = 0;

    // Logging
    function addLog(msg) {
        const t = new Date().toLocaleTimeString();
        logEl.innerHTML = '[' + t + '] ' + msg + '<br>' + logEl.innerHTML;
        const parts = logEl.innerHTML.split('<br>');
        if (parts.length > 160) {
            logEl.innerHTML = parts.slice(0, 160).join('<br>');
        }
    }

    // Number formatting
    function formatNumber(val, digits) {
        if (val === null || val === undefined || !isFinite(val)) return '-';
        return Number(val).toFixed(digits);
    }

    // Push chart values
    function pushValues(payload) {
        const chunk = payload && payload.chunks ? payload.chunks : null;
        if (!chunk || !chunk.x) return;
        
        const len = Math.min(chunk.x.length, chunk.y.length, chunk.z.length);
        for (let i = 0; i < len; i++) {
            // Remove old points if exceeding max
            if (chart.data.labels.length >= maxPoints) {
                chart.data.labels.shift();
                chart.data.datasets[0].data.shift();
                chart.data.datasets[1].data.shift();
                chart.data.datasets[2].data.shift();
            }
            
            // Add new points
            chart.data.labels.push(sampleIndex++);
            chart.data.datasets[0].data.push(chunk.x[i]);
            chart.data.datasets[1].data.push(chunk.y[i]);
            chart.data.datasets[2].data.push(chunk.z[i]);
        }
        
        chart.update('none');
        
        if (payload && payload.mag !== undefined) {
            metrics.mag.textContent = formatNumber(payload.mag, 3);
        }
    }

    // Update metrics display
    function updateMetrics(payload) {
        const stats = payload && payload.s ? payload.s : {};
        metrics.msg.textContent = formatNumber(stats.pps !== undefined ? stats.pps : stats.mps, 0);
        metrics.samples.textContent = formatNumber(stats.sps, 0);
        
        if (payload && payload.mag !== undefined) {
            metrics.mag.textContent = formatNumber(payload.mag, 3);
        }
        
        statusEl.textContent = 'Connected — ' + formatNumber((stats.pps !== undefined ? stats.pps : (stats.mps !== undefined ? stats.mps : 0)), 0) + ' Hz';
    }

    // WebSocket connection
    addLog('Waiting for IIS3DWB data...');
    const wsUrl = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws/data';
    addLog('Connecting to ' + wsUrl);
    
    const ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        addLog('WebSocket connected');
        statusEl.style.borderLeftColor = '#10b981';
        statusEl.style.background = '#fff';
    };
    
    ws.onerror = () => {
        addLog('WebSocket error');
        statusEl.textContent = 'Error';
        statusEl.style.borderLeftColor = '#ef4444';
        statusEl.style.background = '#fef2f2';
    };
    
    ws.onclose = () => {
        addLog('WebSocket closed');
        statusEl.textContent = 'Disconnected';
        statusEl.style.borderLeftColor = '#f59e0b';
        statusEl.style.background = '#fffbeb';
    };
    
    let msgCounter = 0;
    ws.onmessage = (ev) => {
        try {
            const payload = JSON.parse(ev.data);
            pushValues(payload);
            updateMetrics(payload);
            msgCounter++;
            
            if (msgCounter % 200 === 0) {
                const stats = payload && payload.s ? payload.s : {};
                addLog('Plot ' + formatNumber(stats.pps !== undefined ? stats.pps : (stats.mps || 0), 1) + ' pts/s (msg ' + formatNumber(stats.mps || 0, 1) + '), sensor ' + formatNumber(stats.sps || 0, 0) + ' samples/s');
            }
        } catch(err) {
            addLog('Parse error: ' + err.message);
        }
    };
})();
