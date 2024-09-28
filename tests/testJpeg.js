//test capturing a jpeg image using binding.js
const { captureFrameAsJpeg } = require('../lib/binding');
const fs = require('fs');
const path = require('path');

const quality = 85;
const jpegPath = path.join(__dirname, 'test.jpeg');

captureFrameAsJpeg(quality).then((result) => {
    status = result.status;
    jpegBuffer = result.message;
    console.log('size in mb:', jpegBuffer.length / 1024 / 1024);
    if (status === 1) {
        fs.writeFileSync(jpegPath, jpegBuffer);
        console.log(`Jpeg image saved at ${jpegPath}`);
    } else {
        console.log('Error:', status);
    }
} ).catch(err => {
    console.error(err);
});