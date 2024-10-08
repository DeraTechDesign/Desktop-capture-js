// desktop_duplication.js

const { DesktopDuplicator } = require('bindings')('desktop_duplication');

const net = require('net');
const zlib = require('zlib');

const duplicator = new DesktopDuplicator();

const server = net.createServer((socket) => {
    console.log('Client connected');

    let isConnected = true;
    const sendFrame = () => {
        try {
            const frame = duplicator.getFrame();

            // Encode pixel buffers to base64
            frame.dirtyRegions = frame.dirtyRegions.map(region => {
                return {
                    left: region.left,
                    top: region.top,
                    right: region.right,
                    bottom: region.bottom,
                    width: region.width,
                    height: region.height,
                    pixels: region.pixels.toString('base64')
                };
            });

            // Include move regions
            frame.moveRegions = frame.moveRegions.map(move => {
                return {
                    sourcePoint: move,
                    destinationRect: move
                };
            });

            if (frame.dirtyRegions.length === 0 && frame.moveRegions.length === 0) {
                console.log('Warning: Frame has no dirty or move regions.');
                if (isConnected)
                    setTimeout(sendFrame, 1000 / 10);
            }else{
            const data = JSON.stringify(frame) + '\n';
                
                // Count FPS
                if (!this.frameCount) {
                    this.frameCount = 0;
                    this.startTime = Date.now();
                }
                this.frameCount++;
                const elapsedTime = (Date.now() - this.startTime) / 1000;
                const fps = this.frameCount / elapsedTime;
                console.log(`FPS: ${fps.toFixed(2)}`);
                console.log(`Frame size in mb is ${data.length / 1024 / 1024} MB`);
                
                const startTime = Date.now();
                
                // Compress the data asynchronously
                zlib.deflate(data, (err, compressedData) => {
                    const endTime = Date.now();
                    const compressionTime = (endTime - startTime) / 1000;
                    
                    if (err) {
                        console.error('Error compressing data:', err);
                        socket.destroy();
                        return;
                    }
                    
                    console.log(`Compression time: ${compressionTime.toFixed(2)} seconds`);
                    console.log(`Compressed frame size in mb is ${compressedData.length / 1024 / 1024} MB`);
                    socket.write(compressedData);
                    
                    if (isConnected)
                        setTimeout(sendFrame, 1000 / 10);
                });

            }

        } catch (err) {
            console.error('Error capturing frame:', err);
            socket.destroy();
        }
    };

    sendFrame();

    socket.on('end', () => {
        console.log('Client disconnected');
        isConnected = false;
    });

    socket.on('error', (err) => {
        console.error('Socket error:', err);
        isConnected = false;
    });
});

server.listen(12345, () => {
    console.log('Server listening on port 12345');
});
