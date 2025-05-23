from fastapi import FastAPI, WebSocket
import json
import sqlalchemy
import databases
import os
import logging
from contextlib import asynccontextmanager

DATABASE_URL = os.getenv(
    "DATABASE_URL", "postgresql://user:password@postgres:5432/mydb")
database = databases.Database(DATABASE_URL)
metadata = sqlalchemy.MetaData()

messages = sqlalchemy.Table(
    "messages",
    metadata,
    sqlalchemy.Column("id", sqlalchemy.Integer, primary_key=True),
    sqlalchemy.Column("text", sqlalchemy.String),
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
    results = await database.fetch_all(messages.select())
    return {"messages": [dict(row) for row in results]}


@app.websocket("/message")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            data = await websocket.receive_text()
            query = messages.insert().values(text=str(data))
            await database.execute(query)
            logger.info("Received JSON:", extra=str(data))
    except Exception as e:
        logger.error("WebSocket connection closed:", extra=str(e))
