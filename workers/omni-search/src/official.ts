import type { OfficialSite } from "./types";

/** Brand → canonical homepage for navigational queries ("google" → google.com). */
export const OFFICIAL_SITES: Record<string, OfficialSite> = {
  google: {
    url: "https://www.google.com/",
    title: "Google",
    snippet:
      "Search the world's information, including webpages, images, videos and more. Google has many special features to help you find exactly what you're looking for.",
  },
  youtube: {
    url: "https://www.youtube.com/",
    title: "YouTube",
    snippet:
      "Enjoy the videos and music you love, upload original content, and share it all with friends, family, and the world on YouTube.",
  },
  facebook: {
    url: "https://www.facebook.com/",
    title: "Facebook",
    snippet: "Connect with friends, family and other people you know. Share photos and videos, send messages and get updates.",
  },
  instagram: {
    url: "https://www.instagram.com/",
    title: "Instagram",
    snippet: "Create & share photos, stories, Reels & more from your favorite people and brands.",
  },
  twitter: {
    url: "https://x.com/",
    title: "X",
    snippet: "From breaking news and entertainment to sports and politics, get the full story with all the live commentary.",
  },
  x: {
    url: "https://x.com/",
    title: "X",
    snippet: "From breaking news and entertainment to sports and politics, get the full story with all the live commentary.",
  },
  wikipedia: {
    url: "https://www.wikipedia.org/",
    title: "Wikipedia",
    snippet: "Wikipedia is a free online encyclopedia, created and edited by volunteers around the world.",
  },
  amazon: {
    url: "https://www.amazon.com/",
    title: "Amazon.com",
    snippet: "Free shipping on millions of items. Shop online for electronics, apparel, books, and more.",
  },
  github: {
    url: "https://github.com/",
    title: "GitHub",
    snippet: "GitHub is where people build software. More than 100 million people use GitHub to discover, fork, and contribute to over 420 million projects.",
  },
  reddit: {
    url: "https://www.reddit.com/",
    title: "Reddit",
    snippet: "Reddit is a network of communities where people can dive into their interests, hobbies and passions.",
  },
  netflix: {
    url: "https://www.netflix.com/",
    title: "Netflix",
    snippet: "Watch TV shows and movies online. Stream instantly or download on your phone, tablet, or computer.",
  },
  microsoft: {
    url: "https://www.microsoft.com/",
    title: "Microsoft",
    snippet: "Explore Microsoft products and services for your home or business. Shop Surface, Microsoft 365, Xbox, Windows, Azure and more.",
  },
  apple: {
    url: "https://www.apple.com/",
    title: "Apple",
    snippet: "Discover the innovative world of Apple and shop everything iPhone, iPad, Apple Watch, Mac, and Apple TV.",
  },
  cloudflare: {
    url: "https://www.cloudflare.com/",
    title: "Cloudflare",
    snippet: "Cloudflare is the connectivity cloud company — making everything you connect to the Internet secure, private, fast, and reliable.",
  },
  gmail: {
    url: "https://mail.google.com/",
    title: "Gmail",
    snippet: "Gmail is email that's intuitive, efficient, and useful. 15 GB of storage, less spam, and mobile access.",
  },
  chatgpt: {
    url: "https://chatgpt.com/",
    title: "ChatGPT",
    snippet: "ChatGPT helps you get answers, find inspiration, and be more productive.",
  },
  openai: {
    url: "https://openai.com/",
    title: "OpenAI",
    snippet: "OpenAI is an AI research and deployment company. Our mission is to ensure that artificial general intelligence benefits all of humanity.",
  },
};

export function lookupOfficial(query: string): OfficialSite | null {
  const key = query.trim().toLowerCase();
  return OFFICIAL_SITES[key] ?? null;
}
