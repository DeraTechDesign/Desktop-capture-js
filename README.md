# Desktop-capture-js

A native desktop capture library for Node.js that leverages N-API and DirectX libraries to capture desktop on Windows rapidly and efficiently. Ideal for applications requiring real-time screen capturing, such as screenshot taking, streaming, recording, or automated testing tools.

## Table of Contents

- [Installation](#installation)
- [Usage](#usage)
  - [Capturing a Frame as Buffer](#capturing-a-frame-as-buffer)
  - [Capturing a Frame as JPEG](#capturing-a-frame-as-jpeg)
- [API Reference](#api-reference)
- [Examples](#examples)

## Installation

Ensure you have [Node.js](https://nodejs.org/) installed. Then, install the package via npm:

~~~bash
npm install desktop-capture-js
~~~

## Usage

First, require the library in your project:

~~~javascript
const { captureFrameAsBuffer, captureFrameAsJpeg } = require('desktop-capture-js');
~~~

### Capturing a Frame as Buffer

Capture the current desktop frame as a raw buffer along with its dimensions.

~~~javascript
const result = captureFrameAsBuffer();

if (result.status === 1) {
    const frameBuffer = result.message;
    const width = result.width;
    const height = result.height;
    // Process the frame buffer as needed
} else {
    console.error('Capture failed:', result.message);
}
~~~

### Capturing a Frame as JPEG

Capture the current desktop frame and convert it to a JPEG image. You can specify the quality (default is 80).

~~~javascript
captureFrameAsJpeg(90)
    .then(result => {
        if (result.status === 1) {
            const jpegBuffer = result.message;
            const width = result.width;
            const height = result.height;
            // Save or process the JPEG buffer as needed
        } else {
            console.error('No new frame available');
        }
    })
    .catch(error => {
        console.error('Capture failed:', error.message);
    });
~~~

## API Reference

### `captureFrameAsBuffer()`

Captures the current desktop frame as a raw buffer.

**Returns:**

- An object containing:
  - `status` (`number`): `1` for success, `0` for failure.
  - `message` (`Buffer` | `string`): The frame data buffer on success, or an error message.
  - `width` (`number`): Width of the captured frame.
  - `height` (`number`): Height of the captured frame.

### `captureFrameAsJpeg(quality)`

Captures the current desktop frame and converts it to a JPEG image.

**Parameters:**

- `quality` (`number`, optional): JPEG quality (1-100). Defaults to `80`.

**Returns:**

- A `Promise` that resolves to an object containing:
  - `status` (`number`): `1` for success, `0` for failure.
  - `message` (`Buffer` | `string`): The JPEG data buffer on success, or an error message.
  - `width` (`number`): Width of the captured frame.
  - `height` (`number`): Height of the captured frame.

## Examples

### Save Captured Frame as JPEG

~~~javascript
const fs = require('fs');
const { captureFrameAsJpeg } = require('desktop-capture-js');

captureFrameAsJpeg(85)
    .then(result => {
        if (result.status === 1) {
            fs.writeFileSync('screenshot.jpg', result.message);
            console.log(`Screenshot saved: ${result.width}x${result.height}`);
        } else {
            console.error('No new frame available');
        }
    })
    .catch(error => {
        console.error('Error capturing frame:', error.message);
    });
~~~

### Stream Desktop Frames

~~~javascript
const { captureFrameAsBuffer } = require('desktop-capture-js');

setInterval(() => {
    const result = captureFrameAsBuffer();
    if (result.status === 1) {
        // Stream the frame buffer to a server or process it
        console.log(`Captured frame: ${result.width}x${result.height}`);
    } else {
        console.warn(result.message);
    }
}, 17); // Capture in 60 fps
~~~
