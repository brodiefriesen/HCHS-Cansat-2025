<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ground Station</title>
    <script src="main.js"></script>
</head>
<body class="bg-gray-900 text-white flex flex-col items-center justify-center min-h-screen p-4">

    <div class="max-w-lg w-full bg-gray-800 p-6 rounded-lg shadow-lg">
        <h1 class="text-2xl font-bold mb-4 text-center">Ground Station</h1>

        <!-- Live Data Feed -->
        <div class="mb-6">
            <h2 class="text-lg font-semibold mb-2">Live Data</h2>
            <div id="data-feed" class="bg-gray-700 p-4 rounded-lg h-32 overflow-auto text-green-400">
                Waiting for data...
            </div>
        </div>

        <!-- Data Transmission -->
        <div>
            <h2 class="text-lg font-semibold mb-2">Send Data</h2>
            <input id="input-data" type="text" placeholder="Enter command" 
                   class="w-full p-2 rounded-lg text-black focus:outline-none">
            <button id="send-btn" class="w-full mt-3 bg-blue-500 hover:bg-blue-600 text-white font-bold py-2 px-4 rounded-lg">
                Send
            </button>
        </div>

        <!-- Priority Transmission Toggle -->
        <div class="mt-6 text-center">
            <button id="toggle-priority" class="bg-red-500 hover:bg-red-600 text-white py-2 px-4 rounded-lg">
                Enable Priority Transmission
            </button>
        </div>
    </div>

    <script>
        const dataFeed = document.getElementById("data-feed");
        const inputField = document.getElementById("input-data");
        const sendButton = document.getElementById("send-btn");
        const togglePriorityButton = document.getElementById("toggle-priority");

        let priorityMode = false;
        let eventSource;

        // Function to disable SSE
        const disableSSE = () => {
            if (eventSource) {
                eventSource.close();
                console.log("SSE connection closed.");
            }
            dataFeed.innerHTML = `<div>Priority transmission mode enabled. Data will not be received until priority mode is disabled.</div>`;
        };

        // Function to enable SSE
        const enableSSE = () => {
            eventSource = new EventSource("http://192.168.4.1/sse");
            dataFeed.innerHTML = `<div>Listening...</div>`;

            eventSource.onmessage = (event) => {
                const message = event.data;
                console.log("Received:", message);

                // Append new data
                dataFeed.innerHTML += `<div>${message}</div>`;

                // Auto-scroll to bottom
                dataFeed.scrollTop = dataFeed.scrollHeight;
            };

            eventSource.onerror = (error) => {
                console.error("SSE Error:", error);
                dataFeed.innerHTML += `<div class="text-red-400">Connection lost. Trying to reconnect...</div>`;
            };
        };

        // Toggle priority mode
        togglePriorityButton.addEventListener("click", () => {
            priorityMode = !priorityMode;
            if (priorityMode) {
                disableSSE();
                togglePriorityButton.innerText = "Disable Priority Transmission";
            } else {
                enableSSE();
                togglePriorityButton.innerText = "Enable Priority Transmission";
            }
        });

        // Connect to the SSE endpoint initially
        enableSSE();

        // Send Data to ESP32
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
                    console.log("Sent:", data);
                    inputField.value = "";
                } else {
                    console.error("Failed to send data");
                }
            } catch (error) {
                console.error("Error sending data:", error);
            }
        });
    </script>

</body>
</html>
