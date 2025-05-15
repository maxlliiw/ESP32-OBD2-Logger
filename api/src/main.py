from fastapi import FastAPI
import sqlalchemy
import databases
import os
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
    query = messages.insert().values(text="Hello from PostgreSQL!")
    await database.execute(query)
    results = await database.fetch_all(messages.select())
    return {"messages": [dict(row) for row in results]}
