from fastapi import FastAPI, WebSocket
import json
import sqlalchemy
import databases
import os
import logging
from contextlib import asynccontextmanager

DATABASE_URL = os.getenv("DATABASE_URL", "postgresql://user:password@postgres:5432/mydb")
API_KEY = os.getenv("API_KEY")
database = databases.Database(DATABASE_URL)
metadata = sqlalchemy.MetaData()


vehicle_log = sqlalchemy.Table(
    "vehicleLog",
    metadata,
    sqlalchemy.Column("id", sqlalchemy.Integer, primary_key=True),
    sqlalchemy.Column("timestampMS", sqlalchemy.String),
    sqlalchemy.Column("engineSpeed", sqlalchemy.Integer),
    sqlalchemy.Column("vehicleSpeed", sqlalchemy.Integer),
    sqlalchemy.Column("batteryVoltage", sqlalchemy.Float),
    
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

            if ("timestampMS" in json_dict):
                await database.execute(vehicle_log.insert(), [
                    json_dict
                ])
    except Exception as e:
        logger.error("WebSocket connection closed:", extra=str(e))
