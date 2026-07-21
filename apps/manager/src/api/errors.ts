export class ArdorApiError extends Error {
  readonly status: number;
  readonly code: string;
  readonly details?: unknown;

  constructor(status: number, code: string, message: string, details?: unknown) {
    super(message);
    this.name = "ArdorApiError";
    this.status = status;
    this.code = code;
    this.details = details;
  }
}
