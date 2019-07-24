const mqtt = require('mqtt');
const { promises: fsPromises } = require('fs');
const path = require('path');
const address = require('address');

const topicRegexp = /device\/([a-zA-Z0-9_-]+)\/(attributes|configuration)\/([a-zA-Z0-9_-]+)/;

function connectLightelligence() {
  return Promise.all([
    fsPromises.readFile(path.resolve(__dirname, 'device_key.pem'), 'utf-8'),
    fsPromises.readFile(path.resolve(__dirname, 'device_cert.pem'), 'utf-8'),
    fsPromises.readFile(path.resolve(__dirname, 'olt_ca.pem'), 'utf-8'),
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

function sendData(client, deviceId, type, value) {
  return new Promise((resolve, reject) => {
    client.publish('data-ingest', JSON.stringify({
      type,
      value,
      deviceId,
    }), (err) => {
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
  return sendData(oltClient, undefined, 'configuration', {
    ip: address.ip(),
  });
}

function messageToPayload(topic, message) {
  const matched = topic.match(topicRegexp);
  if (!matched) {
    return null;
  }
  const result = {
    deviceId: matched[1],
    type: matched[2],
    value: {},
  };
  result.value[matched[3]] = message;
  return result;
}

function subscribeLocal(localClient, oltClient) {
  return new Promise((resolve, reject) => {
    localClient.on('message', (topic, message) => {
      console.log(topic, message.toString());
      const payload = messageToPayload(topic, JSON.parse(message.toString()));
      if (!payload) {
        console.log(`Unrecognized topic ${topic}`);
        return;
      }
      sendData(oltClient, payload.deviceId, payload.type, payload.value);
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
  .then(([oltClient, localClient]) => Promise.all([
    sendIpAddress(oltClient),
    subscribeLocal(localClient, oltClient),
  ]))
  .catch((err) => {
    console.error(err);
    process.exit(1);
  });
