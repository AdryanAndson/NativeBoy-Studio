#!/usr/bin/env python3
"""Fake home router with UPnP port mapping (InternetGatewayDevice), for the link-cable test.
usage: fake_router.py <port> <log file>   (serves http://127.0.0.1:<port>/desc.xml; chunked answers, like many routers)"""
import sys, http.server, re
port, log = int(sys.argv[1]), sys.argv[2]
DESC = """<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device>
<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><friendlyName>Roteador de teste</friendlyName>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device>
<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList><service>
<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
<controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL>
</service></serviceList></device></deviceList></device></deviceList></device></root>"""
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def chunked(self, code, body):
        b = body.encode()
        self.send_response(code); self.send_header("Content-Type", "text/xml"); self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close"); self.end_headers()
        for i in range(0, len(b), 100): part = b[i:i+100]; self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
        self.wfile.write(b"0\r\n\r\n")
    def do_GET(self):
        if self.path == "/desc.xml": self.chunked(200, DESC)
        else: self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n).decode()
        action = self.headers.get("SOAPAction", "").strip('"').split("#")[-1]
        with open(log, "a") as f: f.write(action + " " + " ".join(re.findall(r"<(NewExternalPort|NewInternalClient|NewProtocol)>([^<]*)<", body) and
                                                           ["%s=%s" % m for m in re.findall(r"<(NewExternalPort|NewInternalClient|NewProtocol)>([^<]*)<", body)]) + "\n")
        if action == "GetExternalIPAddress":
            self.chunked(200, '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
                              '<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1">'
                              '<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>')
        else: self.chunked(200, '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body></s:Body></s:Envelope>')
http.server.HTTPServer(("127.0.0.1", port), H).serve_forever()
