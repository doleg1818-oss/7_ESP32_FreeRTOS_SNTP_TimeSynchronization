from http.server import BaseHTTPRequestHandler, HTTPServer
import json


class RequestHandler(BaseHTTPRequestHandler):

    def do_POST(self):

        if self.path != "/api/data":
            self.send_response(404)
            self.end_headers()
            return

        content_length = int(
            self.headers.get("Content-Length", 0)
        )

        body = self.rfile.read(content_length)

        try:
            data = json.loads(body)

            print("Received JSON:")
            print(json.dumps(data, indent=4))

        except json.JSONDecodeError:
            self.send_response(400)
            self.end_headers()
            return


        response = {
            "status": "ok",
            "message": "data received"
        }

        response_body = json.dumps(response).encode("utf-8")


        self.send_response(200)

        self.send_header(
            "Content-Type",
            "application/json"
        )

        self.send_header(
            "Content-Length",
            str(len(response_body))
        )

        self.end_headers()

        self.wfile.write(response_body)


server = HTTPServer(
    ("0.0.0.0", 8000),
    RequestHandler
)

print("HTTP server started on port 8000")

server.serve_forever()