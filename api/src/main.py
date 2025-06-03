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


vehicle_log = sqlalchemy.Table(
    "vehicleLog3",
    metadata,
    sqlalchemy.Column("id", sqlalchemy.Integer, primary_key=True),
    sqlalchemy.Column("timestamp", sqlalchemy.Integer),
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
    sqlalchemy.Column("CATALYST_TEMP_B1S1", sqlalchemy.Integer),
    sqlalchemy.Column("CATALYST_TEMP_B1S2", sqlalchemy.Integer),
    sqlalchemy.Column("AIR_FUEL_EQUIV_RATIO", sqlalchemy.Integer),
    sqlalchemy.Column("ENGINE_OIL_TEMP", sqlalchemy.Integer),
    sqlalchemy.Column("FUEL_INJECTION_TIMING", sqlalchemy.Integer),
    sqlalchemy.Column("ENGINE_FUEL_RATE", sqlalchemy.Integer)
)

engine = sqlalchemy.create_engine(DATABASE_URL)

logger = logging.getLogger("FASTAPI")


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
    api_key = websocket.query_params.get("api_key")
    if api_key != API_KEY:
        await websocket.close(code=1008)
        print("Unauthorized WebSocket connection attempt")
        return

    await websocket.accept()
    try:
        while True:
            data = await websocket.receive_text()
            json_dict = json.loads(data)
            logger.info("Received JSON:", extra=str(data))

            if ("timestampMS" in json_dict and "pids" in json_dict):
                t = int(json_dict.get("startTime")) * 1000
                t += int(json_dict.get("timestampMS"))

                pids = json_dict.get("pids")

                await database.execute(vehicle_log.insert().values(
                    timestamp=t,
                    VIN=json_dict.get("vin"),
                    BATTERY_VOLTAGE=json_dict.get("volts"),
                    ENGINE_LOAD=pids[0],
                    COOLANT_TEMP=pids[1],
                    SHORT_TERM_FUEL_TRIM_1=pids[2],
                    LONG_TERM_FUEL_TRIM_1=pids[3],
                    SHORT_TERM_FUEL_TRIM_2=pids[4],
                    LONG_TERM_FUEL_TRIM_2=pids[5],
                    FUEL_PRESSURE=pids[6],
                    INTAKE_MAP=pids[7],
                    RPM=pids[8],
                    SPEED=pids[9],
                    TIMING_ADVANCE=pids[10],
                    INTAKE_TEMP=pids[11],
                    MAF_FLOW=pids[12],
                    THROTTLE_POSITION=pids[13],
                    BAROMETRIC_PRESSURE=pids[14],
                    CATALYST_TEMP_B1S1=pids[15],
                    CATALYST_TEMP_B1S2=pids[16],
                    AIR_FUEL_EQUIV_RATIO=pids[17],
                    ENGINE_OIL_TEMP=pids[18],
                    FUEL_INJECTION_TIMING=pids[19],
                    ENGINE_FUEL_RATE=pids[20]
                ))
    except Exception as e:
        logger.error("WebSocket connection closed:", extra=str(e))
