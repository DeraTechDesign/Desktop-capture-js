// a binding library for capturing desktop using native code
const bindings = require('bindings');
const DesktopCapture = bindings('desktop_capture');
const sharp = require('sharp');
const capture = new DesktopCapture.DesktopCapture();

// captureFrameAsBuffer: function to capture frame as buffer
function captureFrameAsBuffer() {
    try {
        const frame = capture.getFrame();
        if (frame && frame.data && frame.data.length > 0) {
            currentFrame = frame;
            return({ status: 1, message: frame.data, width: frame.width, height: frame.height });
        } else {
            return({ status: 0, message: "No new frame available" });
        }
    } catch (err) {
        return({ status: 0, message: err });
    }
}

// captureFrameAsJpeg: function to capture frame as jpeg
function captureFrameAsJpeg(quality = 80) {
    return new Promise(async (resolve, reject) => {
        try {
            const frame = capture.getFrame();
            if (frame && frame.data && frame.data.length > 0) {
                console.log('frame:', frame.width, frame.height, frame.data.length/ 1024 / 1024);
                sharp(frame.data, {
                    raw: {
                        width: frame.width,
                        height: frame.height,
                        channels: 4, 
                        premultiplied: false
                    }
                }).toFormat('jpeg').jpeg({ quality: quality }).toBuffer().then((data) => {
                    resolve({ status: 1, message: data, width: frame.width, height: frame.height });
                }).catch((err) => {
                    reject({ status: 0, message: err });
                });
            } else {
                resolve({ status: 0, message: "No new frame available" });
            }
        } catch (err) {
            reject({ status: 0, message: err });
        }
    });
}

module.exports = { captureFrameAsBuffer, captureFrameAsJpeg };
