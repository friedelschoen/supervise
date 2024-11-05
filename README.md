# Supervisor

A lightweight, peer-to-peer service supervisor designed for efficient service management and dependency handling. This project is focused on providing a minimal yet robust solution for supervising multiple interdependent services.

## Features

- **Service Supervision**: Manages the lifecycle of services, including start, stop, and restart functionality.
- **Dependency Management**: Ensures that each service's dependencies are met before it starts, handling dependencies dynamically as services come online.
- **FIFO-based IPC**: Uses FIFOs to communicate between services, allowing for flexible control and coordination.

## License

This project is licensed under the Zlib License. See the [LICENSE](LICENSE) file for more information.
