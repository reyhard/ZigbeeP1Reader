const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const extend = require('zigbee-herdsman-converters/lib/extend');
const e = exposes.presets;
const ea = exposes.access;

const fzLocal = {
    P1_metering: {
        cluster: 'seMetering',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const payload = {};
            if (msg.data.hasOwnProperty('16384')) {
                payload.energy_low = msg.data['16384'] / 1000.0;
            }
            if (msg.data.hasOwnProperty('16385')) {
                payload.energy_high = msg.data['16385'] / 1000.0;
            }
            if (msg.data.hasOwnProperty('16386')) {
                payload.energy_tariff = msg.data['16386'];
            }
            if (msg.data.hasOwnProperty('16387')) {
                payload.power = msg.data['16387'];
            }
            if (msg.data.hasOwnProperty('16388')) {
                payload.power_l1 = msg.data['16388'][2];
                payload.power_l2 = msg.data['16388'][1];
                payload.power_l3 = msg.data['16388'][0];
            }
            if (msg.data.hasOwnProperty('16389')) {
                payload.gas = msg.data['16389'] / 1000.0;
            }
            return payload;
        },
    },
};


const definition = {
    zigbeeModel: ['LILYGO.P1_Reader'],
    model: 'LILYGO.P1_Reader',
    vendor: 'LilyGO',
    description: 'P1 Electric Meter Reader',
    fromZigbee: [fz.metering, fzLocal.P1_metering],
    toZigbee: [],
    exposes: [
        exposes.numeric('energy_low', ea.STATE).withUnit('kWh').withDescription('Energy low tariff'),
        exposes.numeric('energy_high', ea.STATE).withUnit('kWh').withDescription('Energy high tariff'),
        exposes.numeric('energy_tariff', ea.STATE).withDescription('Current energy tariff'),
        exposes.numeric('power', ea.STATE).withUnit('W').withDescription('Current power consumption'),
        exposes.numeric('power_l1', ea.STATE).withUnit('W').withDescription('Instantaneous active power L1 (+P)'),
        exposes.numeric('power_l2', ea.STATE).withUnit('W').withDescription('Instantaneous active power L2 (+P)'),
        exposes.numeric('power_l3', ea.STATE).withUnit('W').withDescription('Instantaneous active power L3 (+P)'),
        exposes.numeric('gas', ea.STATE).withUnit('m3').withDescription('Gas consumption'),
    ],
    // The configure method below is needed to make the device reports on/off state changes
    // when the device is controlled manually through the button on it.
    configure: async (device, coordinatorEndpoint, logger) => {
        const endpoint = device.getEndpoint(1);
        await reporting.bind(endpoint, coordinatorEndpoint, ['seMetering']);
        //await reporting.energy(endpoint);
        //await reporting.readMeteringMultiplierDivisor(endpoint);
    },
};

module.exports = definition;
