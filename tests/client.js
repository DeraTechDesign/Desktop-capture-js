const net = require('net');
const express = require('express');
const zlib = require('zlib');
const app = express();
const port = 3006;
const { BitmapProcessor } = require('bindings')('bitmap_processor');


// CORS middleware
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    next();
});

let latestBitmapBuffer = null;

app.get('/latestFrame', (req, res) => {
    if (latestBitmapBuffer) {
        res.setHeader('Content-Type', 'image/bmp');
        res.send(latestBitmapBuffer);
    } else {
        res.status(404).send('No frame available');
    }
});

app.listen(port, () => {
    console.log(`Server listening on port ${port}`);
});

// Initialize variables
let bitmapProcessor = null;
let initialized = false;

const socket = net.connect(12345, () => {
    console.log('Connected to server');
});

let buffer = '';
let compressedData = '';

socket.on('data', (data) => {
    compressedData += data;

    zlib.inflate(data, (err, decompressedData) => {
        if (err) {
            return;
        }
        buffer += decompressedData.toString();
        compressedData = '';
    });

    let boundary = buffer.indexOf('\n');
    while (boundary !== -1) {
        const frameData = buffer.substring(0, boundary);
        buffer = buffer.substring(boundary + 1);

        try {
            const frame = JSON.parse(frameData);

            if (!initialized) {
                // Initialize bitmap processor with dimensions from the server
                bitmapProcessor = new BitmapProcessor();
                bitmapProcessor.initializeBitmap(frame.width, frame.height);
                initialized = true;
                console.log(`Initialized bitmap with dimensions: ${frame.width}x${frame.height}`);
            }

            // Decode dirty regions
            const dirtyRegions = frame.dirtyRegions.map(region => {
                const pixelsBuffer = Buffer.from(region.pixels, 'base64');
                return {
                    left: region.left,
                    top: region.top,
                    width: region.width,
                    height: region.height,
                    pixels: pixelsBuffer
                };
            });
            // Apply dirty regions using the N-API addon
            console.time('applyDirtyRegions');
            bitmapProcessor.applyDirtyRegions(dirtyRegions);
            console.timeEnd('applyDirtyRegions');

            // Apply move regions
            console.time('applyMoveRegions');
            bitmapProcessor.applyMoveRegions(frame.moveRegions);
            console.timeEnd('applyMoveRegions');

            // Get the updated bitmap buffer
            console.time('getBitmapBuffer');
            latestBitmapBuffer = bitmapProcessor.getBitmapBuffer();
            console.timeEnd('getBitmapBuffer');

        } catch (err) {
            console.error('Error parsing frame data:', err);
        }

        boundary = buffer.indexOf('\n');
    }
});

socket.on('end', () => {
    console.log('Disconnected from server');
});

socket.on('error', (err) => {
    console.error('Socket error:', err);
});
