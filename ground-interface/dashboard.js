// dashboard.js

const dataFeed = document.getElementById("data-feed");
const inputField = document.getElementById("input-data");
const sendButton = document.getElementById("send-btn");
const priorityToggle = document.getElementById("priority-toggle");
const staleWarning = document.getElementById("stale-warning");

// Card elements
const tempBox = document.getElementById("temp-box");
const wheelBox = document.getElementById("wheel-box");
const chuteBox = document.getElementById("chute-box");


// Chart.js line charts
const accChart = createChart("acc-chart", "Accelerometer", ["X", "Y", "Z"], -20000, 20000);
const gyroChart = createChart("gyro-chart", "Gyroscope", ["X", "Y", "Z"], -400, 400);
const pitchYawChart = createChart("pitch-yaw-chart", "Pitch/Yaw", ["Pitch", "Yaw"], -90, 90);
const magChart = createChart("mag-chart", "Magnetometer", ["X", "Y", "Z"], 0, 1);

let eventSource = null;
let lastDataTime = Date.now();
let staleTimeout = 5000; // 5 seconds

var priorityMode = 0;

function createChart(canvasId, label, seriesLabels, yMin = -200, yMax = 200) {
    const ctx = document.getElementById(canvasId).getContext("2d");
    return new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: seriesLabels.map((label, idx) => ({
                label,
                borderColor: ['#f87171', '#60a5fa', '#34d399', '#facc15'][idx],
                data: [],
                fill: false,
                tension: 0.2
            }))
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            scales: {
                x: { display: false },
                y: {
                    beginAtZero: false,
                    suggestedMin: yMin,
                    suggestedMax: yMax
                }
            }
        }
    });
}


function updateChart(chart, values) {
    const now = new Date().toLocaleTimeString();
    chart.data.labels.push(now);
    chart.data.labels = chart.data.labels.slice(-20);
    values.forEach((v, i) => {
        chart.data.datasets[i].data.push(v);
        chart.data.datasets[i].data = chart.data.datasets[i].data.slice(-20);
    });
    chart.update();
}

function parseReport(report) {
    if (!report.startsWith("DWL:1:")) return;

    const data = {};
    const fields = report.split(":");

    for (let i = 0; i < fields.length; i++) {
        switch (fields[i]) {
            case "ACC":
                data.acc = fields[++i].split(",").map(parseFloat);
                break;
            case "GY":
                data.gyro = fields[++i].split(",").map(parseFloat);
                break;
            case "PITCH":
                data.pitch = parseFloat(fields[++i]);
                break;
            case "YAW":
                data.yaw = parseFloat(fields[++i]);
                break;
            case "MAG":
                data.mag = fields[++i].split(",").map(Number);
                break;
            case "TEMP":
                data.temp = parseFloat(fields[++i]);
                break;
            case "WHEEL":
                data.wheel = parseInt(fields[++i]);
                break;
            case "CHUTE":
                data.chute = parseInt(fields[++i]);
                break;
        }
    }

    return data;
}

function updateUI(parsed) {
    if (!parsed) return;

    updateChart(accChart, parsed.acc);
    updateChart(gyroChart, parsed.gyro);
    updateChart(pitchYawChart, [parsed.pitch, parsed.yaw]);
    updateChart(magChart, parsed.mag);

    tempBox.textContent = `${parsed.temp.toFixed(2)} Â°C`;
    wheelBox.textContent = parsed.wheel ?? '-';
    chuteBox.textContent = parsed.chute ? 'Deployed' : 'Not Deployed';
}

function resetStaleTimer() {
    lastDataTime = Date.now();
    staleWarning.classList.add("hidden");
}

function checkStale() {
    if (Date.now() - lastDataTime > staleTimeout) {
        staleWarning.classList.remove("hidden");
    }
}

function startSSE() {
    if (eventSource) {
        eventSource.close();
    }
    eventSource = new EventSource("http://192.168.4.1/sse");
    eventSource.onmessage = (event) => {
        const message = event.data;
        resetStaleTimer();
        updateUI(parseReport(message));
    };
    eventSource.onerror = () => {
        console.error("SSE connection lost");
    };
}

sendButton.addEventListener("click", async () => {
    const data = inputField.value.trim();
    if (!data) return;

    try {
        const response = await fetch("http://192.168.4.1/instruction", {
            method: "POST",
            headers: { "Content-Type": "text/plain" },
            body: data
        });
        if (response.ok) {
            inputField.value = "";
        }
    } catch (error) {
        console.error("Failed to send data", error);
    }
});



        // Toggle priority mode
        priorityToggle.addEventListener("click", () => {
            priorityMode = !priorityMode;
            if (priorityMode) {
                if (eventSource) eventSource.close();
                priorityToggle.innerText = "Disable Priority Transmission";
            } else {
                startSSE();
                priorityToggle.innerText = "Enable Priority Transmission";
            }
        });


setInterval(checkStale, 1000);
startSSE();
