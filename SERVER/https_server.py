from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import ssl
import time
import threading


# ============================================================
# CONFIG
# ============================================================

SERVER_IP = "0.0.0.0"
SERVER_PORT = 8443

CERT_FILE = "server_cert.pem"
KEY_FILE = "server_key.pem"

# ESP32 має timeout приблизно 10 секунд.
# Тому сервер спеціально не відповідає 15 секунд.
FAULT_RESPONSE_DELAY_SEC = 15


# ============================================================
# IDEMPOTENCY / DUPLICATE STORAGE
# ============================================================

# Тут сервер пам'ятає вже оброблені повідомлення.
#
# Ключ:
#     (device_id, message_id)
#
# Наприклад:
#     (17, 1)
#     (17, 2)
#     (18, 1)
#
processed_messages = set()

# Оскільки використовуємо ThreadingHTTPServer,
# захищаємо shared set mutex-ом.
processed_messages_lock = threading.Lock()


class RequestHandler(BaseHTTPRequestHandler):

    # ========================================================
    # HELPER: SEND JSON RESPONSE
    # ========================================================

    def send_json_response(self, status_code, response):

        response_body = json.dumps(
            response
        ).encode("utf-8")

        self.send_response(status_code)

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


    # ========================================================
    # POST
    # ========================================================

    def do_POST(self):

        # ----------------------------------------------------
        # 1. CHECK ENDPOINT
        # ----------------------------------------------------

        if self.path not in (
            "/api/data",
            "/api/data-loss-test",
            "/api/error500"
        ):
            self.send_json_response(
                404,
                {
                    "status": "error",
                    "message": "endpoint not found"
                }
            )
            return


        # ----------------------------------------------------
        # 2. TEST: HTTP 500
        # ----------------------------------------------------

        if self.path == "/api/error500":

            self.send_json_response(
                500,
                {
                    "status": "error",
                    "message": "intentional test error"
                }
            )

            return


        # ----------------------------------------------------
        # 3. READ HTTP BODY
        # ----------------------------------------------------

        content_length = int(
            self.headers.get(
                "Content-Length",
                0
            )
        )

        body = self.rfile.read(
            content_length
        )


        # ----------------------------------------------------
        # 4. PARSE JSON
        # ----------------------------------------------------

        try:

            data = json.loads(body)

        except json.JSONDecodeError:

            self.send_json_response(
                400,
                {
                    "status": "error",
                    "message": "invalid JSON"
                }
            )

            return


        print()
        print("========================================")
        print(f"POST {self.path}")
        print("Received JSON:")
        print(json.dumps(data, indent=4))


        # ----------------------------------------------------
        # 5. CHECK REQUIRED FIELDS
        # ----------------------------------------------------

        message_id = data.get("message_id")
        device_id = data.get("device_id")

        if message_id is None or device_id is None:

            self.send_json_response(
                400,
                {
                    "status": "error",
                    "message": "message_id or device_id missing"
                }
            )

            return


        # ----------------------------------------------------
        # 6. IDEMPOTENCY KEY
        # ----------------------------------------------------

        message_key = (
            device_id,
            message_id
        )


        # ====================================================
        # NORMAL ENDPOINT
        # ====================================================

        if self.path == "/api/data":

            print(
                f"NORMAL request: "
                f"device_id={device_id}, "
                f"message_id={message_id}"
            )

            self.send_json_response(
                200,
                {
                    "status": "ok",
                    "message": "data received via HTTPS",
                    "message_id": message_id
                }
            )

            return


        # ====================================================
        # LOST RESPONSE + DUPLICATE TEST
        # ====================================================

        if self.path == "/api/data-loss-test":

            # ------------------------------------------------
            # Atomic duplicate check
            # ------------------------------------------------

            with processed_messages_lock:

                if message_key in processed_messages:

                    is_duplicate = True

                else:

                    processed_messages.add(
                        message_key
                    )

                    is_duplicate = False


            # ------------------------------------------------
            # DUPLICATE
            # ------------------------------------------------

            if is_duplicate:

                print(
                    f"DUPLICATE message detected: "
                    f"{message_key}"
                )

                print(
                    "SIDE EFFECT SKIPPED"
                )

                print(
                    "Sending 200 OK to stop ESP32 retry"
                )

                self.send_json_response(
                    200,
                    {
                        "status": "ok",
                        "duplicate": True,
                        "message_id": message_id
                    }
                )

                return


            # ------------------------------------------------
            # FIRST TIME
            # ------------------------------------------------

            print(
                f"NEW message: "
                f"{message_key}"
            )

            # Тут імітуємо реальну дію.
            #
            # У реальному проєкті тут могло б бути:
            #
            # save_to_database(data)
            # update_device_state(data)
            # send_command(...)
            # activate_output(...)
            #
            print(
                "SIDE EFFECT EXECUTED"
            )


            # ------------------------------------------------
            # FAULT INJECTION
            # ------------------------------------------------
            #
            # Дані вже оброблені.
            #
            # Але response ESP32 навмисно не надсилаємо.
            #
            # ESP32 через ~10 секунд отримає timeout і
            # повторить ТОЙ САМИЙ message_id.
            #
            # ThreadingHTTPServer дозволить прийняти retry,
            # поки цей handler ще sleep().
            #

            print(
                f"TEST: response intentionally lost "
                f"for {message_key}"
            )

            print(
                f"Waiting {FAULT_RESPONSE_DELAY_SEC} seconds "
                f"without HTTP response..."
            )

            time.sleep(
                FAULT_RESPONSE_DELAY_SEC
            )

            print(
                f"Original connection finished "
                f"without response for {message_key}"
            )

            return


# ============================================================
# CREATE HTTPS SERVER
# ============================================================

server = ThreadingHTTPServer(
    (
        SERVER_IP,
        SERVER_PORT
    ),
    RequestHandler
)


# ============================================================
# TLS CONFIGURATION
# ============================================================

tls_context = ssl.SSLContext(
    ssl.PROTOCOL_TLS_SERVER
)

tls_context.load_cert_chain(
    certfile=CERT_FILE,
    keyfile=KEY_FILE
)

server.socket = tls_context.wrap_socket(
    server.socket,
    server_side=True
)


# ============================================================
# START SERVER
# ============================================================

print(
    f"HTTPS server started on port {SERVER_PORT}"
)

print(
    "Available endpoints:"
)

print(
    "  POST /api/data"
)

print(
    "  POST /api/data-loss-test"
)

print(
    "  POST /api/error500"
)

print()


server.serve_forever()