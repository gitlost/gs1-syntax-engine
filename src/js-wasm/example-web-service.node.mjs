/*
 *  Basic node.js example web service that uses the JavaScript wrapper around
 *  the WASM build of the GS1 Barcode Syntax Engine.
 *
 *  Requirements:
 *
 *    - Run with Node.js >= v18 for stable ES6 module support
 *
 *  Copyright (c) 2022-2025 GS1 AISBL.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *
 *  Example calls:
 *
 *    $ curl -i 'http://127.0.0.1:3030/aiDataStr?input=(02)12312312312319(99)ASDFEE&includeDataTitlesInHRI'
 *
 *    HTTP/1.1 422 Bad Request
 *    Content-Type: text/plain
 *    ...
 *    Required AIs for AI (02) are not satisfied: 37
 *
 *
 *    $ curl -i 'http://127.0.0.1:3030/aiDataStr?input=(02)12312312312319(99)ASDFEE&includeDataTitlesInHRI&noValidateRequisiteAIs'
 *    HTTP/1.1 200 OK
 *    Content-Type: text/plain
 *    ...
 *    Barcode message:      ^021231231231231999ASDFEE
 *    AI element string:    (02)12312312312319(99)ASDFEE
 *    GS1 Digital Link URI: ⧚ Cannot create a DL URI without a primary key AI ⧚
 *    HRI:
 *           CONTENT (02) 12312312312319
 *           INTERNAL (99) ASDFEE
 *
 *    $ curl -i 'http://127.0.0.1:3030/dataStr?input=HTTPS://ID.EXAMPLE.ORG/01/12312312312319?99=ASDFEE&output=json'
 *    HTTP/1.1 200 OK
 *    Content-Type: application/json
 *    ...
 *    {"dataStr":"http://ID.EXAMPLE.ORG/01/12312312312319?99=ASDFEE","aiDataStr":"(01)12312312312319(99)ASDFEE","dlURI":"https://id.gs1.org/01/12312312312319?99=ASDFEE","hri":["(01) 12312312312319","(99) ASDFEE"]}
 *
 */

"use strict";

import { GS1encoder } from "./gs1encoder.mjs";

var gs1encoder = new GS1encoder();
await gs1encoder.init();

import * as http from 'http';
import * as url from 'url';


/*
 *  User-defined tunables
 *
 */
const bind = '127.0.0.1';  // "0.0.0.0" to allow remote connections
const port = 3030;


http.createServer(function(req, res) {

    var parse = url.parse(req.url, true);
    var pathname = parse.pathname;
    var params = parse.query;

    if (req.method !== 'GET') {
        res.writeHead(405, {'Content-Type': 'text/plain'});
        res.write("Method Not Allowed\n");
        res.end();
        return;
    }

    var inpStr = params.input;
    if (!inpStr) {
        res.writeHead(400, {'Content-Type': 'text/plain'});
        res.write("Bad Request: 'input' query parameter must be defined\n");
        res.end();
        return;
    }

    try {

        /*
         *  Example of setting options
         *
         */
        gs1encoder.includeDataTitlesInHRI = "includeDataTitlesInHRI" in params;
        gs1encoder.permitUnknownAIs = "permitUnknownAIs" in params;
        gs1encoder.permitZeroSuppressedGTINinDLuris = "permitZeroSuppressedGTINinDLuris" in params;
        gs1encoder.setValidationEnabled(GS1encoder.validation.RequisiteAIs, !("noValidateRequisiteAIs" in params));

        /*
         *  Set the data
         *
         */
        switch (pathname) {
            case '/dataStr':
                gs1encoder.dataStr = inpStr;
                break;
            case '/aiDataStr':
                gs1encoder.aiDataStr = inpStr;
                break;
            default:
                res.writeHead(404, {'Context-Type': 'text/plain'});
                res.end('Not Found');
                return;
        }

    } catch (err) {
        res.writeHead(422, {'Content-Type': 'text/plain'});
        res.write(err.message + "\n");
        var markup = gs1encoder.errMarkup;
        if (markup)
            res.write(markup.replace(/\|/g, "⧚") + "\n");
        res.end();
        return;
    }

    var dataStr = gs1encoder.dataStr;
    var aiDataStr = gs1encoder.aiDataStr;
    var dlURI; var dlURIerr;
    try { dlURI = gs1encoder.getDLuri(null); } catch (err) { dlURI = null; dlURIerr = err; }
    var hri = gs1encoder.hri;

    if (params.output === 'json') {

        res.writeHead(200, {'Content-Type': 'application/json'});
        var ret = {
            dataStr: dataStr,
            aiDataStr: aiDataStr,
            dlURI: dlURI,
            hri: hri
        }
        res.write(JSON.stringify(ret) + "\n");
        res.end();

    } else {  // Plaintext response

        res.writeHead(200, {'Content-Type': 'text/plain'});
        res.write("Barcode message:      " + gs1encoder.dataStr + "\n");
        res.write("AI element string:    " + (aiDataStr ?? "⧚ Not AI-based data ⧚") + "\n");
        res.write("GS1 Digital Link URI: " + (dlURI ?? "⧚ " + dlURIerr.message + " ⧚") + "\n");
        res.write("HRI:                  " + (dataStr !== "" && hri.length == 0 ? "⧚ Not AI-based data ⧚": "") + "\n");
        hri.forEach(ai => res.write("       " + ai + "\n"));
        res.end();

    }

}).listen(port, bind);

console.log("Web service is running on %s:%d", bind, port);

