from fastapi import FastAPI, WebSocket
import json
import sqlalchemy
import databases
import os
import logging
from contextlib import asynccontextmanager

DATABASE_URL = os.getenv(
    "DATABASE_URL", "postgresql://user:password@postgres:5432/mydb")
API_KEY = os.getenv("API_KEY")
database = databases.Database(DATABASE_URL)
metadata = sqlalchemy.MetaData()

pid_map = {
    '4': 'ENGINE_LOAD',
    '5': 'COOLANT_TEMP',
    '6': 'SHORT_TERM_FUEL_TRIM_1',
    '7': 'LONG_TERM_FUEL_TRIM_1',
    '8': 'SHORT_TERM_FUEL_TRIM_2',
    '9': 'LONG_TERM_FUEL_TRIM_2',
    '10': 'FUEL_PRESSURE',
    '11': 'INTAKE_MAP',
    '12': 'RPM',
    '13': 'SPEED',
    '14': 'TIMING_ADVANCE',
    '15': 'INTAKE_TEMP',
    '16': 'MAF_FLOW',
    '17': 'THROTTLE_POSITION',
    '51': 'BAROMETRIC_PRESSURE',
    '68': 'AIR_FUEL_EQUIV_RATIO'
}

vehicle_log = sqlalchemy.Table(
    "CarDataLog",
    metadata,
    sqlalchemy.Column("id", sqlalchemy.Integer, primary_key=True),
    sqlalchemy.Column("timestamp", sqlalchemy.BigInteger),
    sqlalchemy.Column("VIN", sqlalchemy.String),
    sqlalchemy.Column("BATTERY_VOLTAGE", sqlalchemy.Float),
    sqlalchemy.Column("ENGINE_LOAD", sqlalchemy.Integer),
    sqlalchemy.Column("COOLANT_TEMP", sqlalchemy.Integer),
    sqlalchemy.Column("SHORT_TERM_FUEL_TRIM_1", sqlalchemy.Integer),
    sqlalchemy.Column("LONG_TERM_FUEL_TRIM_1", sqlalchemy.Integer),
    sqlalchemy.Column("SHORT_TERM_FUEL_TRIM_2", sqlalchemy.Integer),
    sqlalchemy.Column("LONG_TERM_FUEL_TRIM_2", sqlalchemy.Integer),
    sqlalchemy.Column("FUEL_PRESSURE", sqlalchemy.Integer),
    sqlalchemy.Column("INTAKE_MAP", sqlalchemy.Integer),
    sqlalchemy.Column("RPM", sqlalchemy.Integer),
    sqlalchemy.Column("SPEED", sqlalchemy.Integer),
    sqlalchemy.Column("TIMING_ADVANCE", sqlalchemy.Integer),
    sqlalchemy.Column("INTAKE_TEMP", sqlalchemy.Integer),
    sqlalchemy.Column("MAF_FLOW", sqlalchemy.Integer),
    sqlalchemy.Column("THROTTLE_POSITION", sqlalchemy.Integer),
    sqlalchemy.Column("BAROMETRIC_PRESSURE", sqlalchemy.Integer),
    sqlalchemy.Column("AIR_FUEL_EQUIV_RATIO", sqlalchemy.Integer),
)

engine = sqlalchemy.create_engine(DATABASE_URL)

logger = logging.getLogger("fastapi")
logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler()
formatter = logging.Formatter(
    '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)


# Create the FastAPI app with a lifespan context manager
# to manage the database connection lifecycle
# and ensure it is closed properly on shutdown.
# This is important to avoid connection leaks and ensure
# that all resources are cleaned up when the app is stopped.
@asynccontextmanager
async def lifespan(app: FastAPI):
    # startup
    await database.connect()
    metadata.create_all(engine)
    try:
        yield
    finally:
        # shutdown
        await database.disconnect()


app = FastAPI(lifespan=lifespan)


@app.get("/")
async def read_root():
    results = await database.fetch_all(vehicle_log.select())
    return {"messages": [dict(row) for row in results]}


@app.delete("/logs/clear")
async def clear_logs():
    query = vehicle_log.delete()
    await database.execute(query)
    return {"status": "success", "message": "All vehicle logs have been deleted."}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    # api_key = websocket.query_params.get("api_key")
    # if api_key != API_KEY:
    #     await websocket.close(code=1008)
    #     print("Unauthorized WebSocket connection attempt")
    #     return

    await websocket.accept()
    try:
        while True:
            data = await websocket.receive_text()
            logger.info("Received DATA: " + data)

            json_dict = json.loads(data)

            if ("obd" in json_dict):
                obd = json.get("obd")
                vin = obd.get("vin")
                voltage = obd.get("battery") / 100
                pids = obd.get("pids")
                values = {
                    pid_map[k]: v for k, v in pids.items()
                }
                t = int(json_dict.get("st")) * 1000
                t += int(json_dict.get("ts"))
                values["timestamp"] = t
                values["VIN"] = vin
                values["BATTERY_VOLTAGE"] = float(voltage) / 100

                await database.execute(vehicle_log.insert().values(values))

    except Exception:
        logger.error("WebSocket connection closed:")
