const mqtt = require('mqtt');
const fs = require('fs');
const { promisify } = require('util');
const path = require('path');
const address = require('address');
const isUuid = require('uuid-validate');

const readFile = promisify(fs.readFile);

const topicRegexp = /device\/([a-zA-Z0-9_-]+)\/(attributes|configuration)\/([a-zA-Z0-9_-]+)/;

function connectLightelligence() {
  return Promise.all([
    readFile(path.resolve(__dirname, 'certs/device_key.pem'), 'utf-8'),
    readFile(path.resolve(__dirname, 'certs/device_cert.pem'), 'utf-8'),
    readFile(path.resolve(__dirname, 'certs/olt_ca.pem'), 'utf-8'),
  ])
    .then(([deviceKey, deviceCert, oltCa]) => new Promise((resolve, reject) => {
      const client = mqtt.connect('mqtts://mqtt.lightelligence.io', {
        key: deviceKey,
        cert: deviceCert,
        ca: oltCa,
      });
      client.once('connect', () => resolve(client));
      client.once('error', reject);
    }));
}

function connectLocal() {
  return new Promise((resolve, reject) => {
    const brokerAddress = process.env.MQTT_BROKER || 'mqtt://localhost';
    const client = mqtt.connect(brokerAddress);
    client.once('connect', () => resolve(client));
    client.once('error', reject);
  });
}

function sendData(client, payload) {
  return new Promise((resolve, reject) => {
    client.publish('data-ingest', JSON.stringify(payload), (err) => {
      if (err) {
        reject(err);
        return;
      }
      resolve();
    });
  });
}

function sendIpAddress(oltClient) {
  setTimeout(() => {
    sendIpAddress(oltClient);
  }, 600 * 1000);
  return sendData(oltClient, {
    type: 'configuration',
    value: {
      ip: address.ip(),
    },
  });
}

function messageToPayload(topic, message) {
  const matched = topic.match(topicRegexp);
  if (!matched) {
    return null;
  }

  const result = {
    type: matched[2],
    value: {},
  };
  if (isUuid(matched[1])) {
    result.deviceId = matched[1]; // eslint-disable-line
  } else {
    result.alias = matched[1]; // eslint-disable-line
  }
  result.value[matched[3]] = message;
  return result;
}

function subscribeLocal(localClient, oltClient) {
  return new Promise((resolve, reject) => {
    localClient.on('message', (topic, message) => {
      console.log(topic, message.toString());
      let msg;
      try {
        msg = JSON.parse(message.toString());
      } catch (e) {
        msg = message.toString();
      }
      const payload = messageToPayload(topic, msg);
      if (!payload) {
        console.log(`Unrecognized topic ${topic}`);
        return;
      }
      sendData(oltClient, payload);
    });
    localClient.subscribe('#', (err) => {
      if (err) {
        reject(err);
        return;
      }
      resolve();
    });
  });
}

Promise.all([
  connectLightelligence(),
  connectLocal(),
])
  .then(([oltClient, localClient]) => {
    console.log('Connected to MQTT endpoints');
    return Promise.all([
      sendIpAddress(oltClient),
      subscribeLocal(localClient, oltClient),
    ]);
  })
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });
