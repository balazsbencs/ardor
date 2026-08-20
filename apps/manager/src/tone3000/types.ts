export type Tone3000User = {
  id: number;
  username: string;
  avatar_url: string | null;
  url: string;
};

export type Tone3000Tone = {
  id: number;
  title: string;
  description: string | null;
  gear: string;
  images: string[] | null;
  format: string;
  license: string;
  user: Tone3000User;
  url: string;
};

export type Tone3000Model = {
  id: number;
  name: string;
  size: string;
  tone_id: number;
  architecture_version: "1" | "2" | "custom" | null;
};

export type Tone3000Selection = {
  tone: Tone3000Tone;
  models: Tone3000Model[];
};
