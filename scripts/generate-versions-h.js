#!/usr/bin/env node
const fs = require('fs');
const common = require('./common.js');
const execSync = require('child_process').execSync;

const versionPropertyPrefixes = ['firmware', 'deviceProtocol', 'moduleProtocol', 'userConfig', 'hardwareConfig', 'smartMacros'];
const patchVersions = ['Major', 'Minor', 'Patch'];
const package = JSON.parse(fs.readFileSync(`${__dirname}/package.json`));

const versionVariables = versionPropertyPrefixes.map(versionPropertyPrefix => {
    const versionPropertyName = `${versionPropertyPrefix}Version`
    const versionPropertyValues = package[versionPropertyName].split('.');
    return patchVersions.map(patchVersion => {
        const versionPropertyValue = versionPropertyValues.shift();
        const versionPropertyMacroName = `${versionPropertyPrefix}${patchVersion}Version`.split(/(?=[A-Z])/).join('_').toUpperCase()
        return `    #define ${versionPropertyMacroName} ${versionPropertyValue}`;
    }).join('\n') + '\n';
}).join('\n');

const gitInfo = common.getGitInfo();

const deviceMd5Sums = package.devices.map(device => {
    if (process.argv.includes('--withMd5Sums')) {
        const md5 = execSync(`md5sum "${__dirname}/../${device.source}" | sed 's/ .*//g'`).toString().trim()
        const line = `    [${device.deviceId}] = "${md5}",`
        return line;
    } else {
        return '"000000000000000000000000000000000",';
    }
}).join('\n');

const moduleMd5Sums = package.modules.map(module => {
    if (process.argv.includes('--withMd5Sums')) {
        const md5 = execSync(`md5sum "${__dirname}/../${module.source}" | sed 's/ .*//g'`).toString().trim()
        const line = `    [${module.moduleId}] = "${md5}",`
        return line;
    } else {
        return `"000000000000000000000000000000000",`;
    }
}).join('\n');


fs.writeFileSync(`${__dirname}/../shared/versions.h`,
`// Please do not edit this file by hand!
// It is to be regenerated by /scripts/generate-versions-h.js

#ifndef __VERSIONS_H__
#define __VERSIONS_H__

// Includes:

    #include "versioning.h"
    #include "slave_protocol.h"

// Variables:

${versionVariables}

#define GIT_REPO "${gitInfo.repo}"
#define GIT_TAG "${gitInfo.tag}"

#ifdef DEVICE_ID
char const * const DeviceMD5Checksums[DEVICE_COUNT+1] = {
${deviceMd5Sums}
};
#endif


#ifdef MODULE_ID
char const * const ModuleMD5Checksums[ModuleId_AllCount] = {
${moduleMd5Sums}
};
#endif

#endif
`);
