<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ground Station</title>
    <script src="chart.js"></script>
    <script src="main.js" defer></script>
    <style>
        .tile {
            background-color: #2d3748;
            border-radius: 0.5rem;
            padding: 1rem;
            flex: 1 1 45%;
            margin: 0.5rem;
        }
    </style>
</head>
<body class="bg-gray-900 text-white flex flex-col items-center justify-center min-h-screen p-4">
    <div class="max-w-6xl w-full bg-gray-800 p-6 rounded-lg shadow-lg">
        <h1 class="text-2xl font-bold mb-4 text-center">Ground Station</h1>

        <!-- Controls -->
        <div class="mb-6 text-center">
            <button id="priority-toggle" class="bg-red-500 hover:bg-red-600 text-white font-bold py-2 px-4 rounded-lg">
                Enable Priority Transmission
            </button>
            <div id="stale-warning" class="text-yellow-400 mt-2 hidden">Data is stale!</div>
        </div>

        <!-- Dashboard -->
        <div class="grid grid-cols-2 gap-4">
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Accelerometer</h2>
                <canvas id="acc-chart"></canvas>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Gyroscope</h2>
                <canvas id="gyro-chart"></canvas>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Pitch / Yaw</h2>
                <canvas id="pitch-yaw-chart"></canvas>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Magnetometer</h2>
                <canvas id="mag-chart"></canvas>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Temperature</h2>
                <div id="temp-box">-- °C</div>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Reaction Wheel</h2>
                <div id="wheel-box">--</div>
            </div>
            <div class="tile h-[300px] bg-gray-700 p-4 rounded-xl">
                <h2 class="font-semibold">Parachute</h2>
                <div id="chute-box">--</div>
            </div>
        </div>

        <!-- Transmission -->
        <div class="mt-6">
            <h2 class="text-lg font-semibold mb-2">Send Command</h2>
            <input id="input-data" type="text" placeholder="Enter command" 
                   class="w-full p-2 rounded-lg text-black focus:outline-none">
            <button id="send-btn" class="w-full mt-3 bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded-lg">
                Send
            </button>
        </div>
    </div>

    <script src="dashboard.js"></script>
    <script src="other.js"></script>
</body>
</html>
