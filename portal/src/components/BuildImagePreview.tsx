/* eslint-disable @next/next/no-img-element */
"use client";

import { useState } from "react";

type BuildImage = {
  alt: string;
  src: string;
};

export function BuildImagePreview({
  className = "",
  images,
}: {
  className?: string;
  images: BuildImage[];
}) {
  const [activeImage, setActiveImage] = useState<BuildImage | null>(null);

  return (
    <>
      <div className={`air-build-photo-grid${className ? ` ${className}` : ""}`}>
        {images.map((image) => (
          <button
            aria-label={image.alt}
            className="air-build-photo"
            key={image.src}
            onClick={() => setActiveImage(image)}
            type="button"
          >
            <img alt={image.alt} src={image.src} />
          </button>
        ))}
      </div>

      {activeImage ? (
        <div
          aria-modal="true"
          className="air-image-preview"
          onClick={() => setActiveImage(null)}
          role="dialog"
        >
          <button
            aria-label="Close image preview"
            className="air-image-preview-close"
            onClick={() => setActiveImage(null)}
            type="button"
          >
            ×
          </button>
          <img
            alt={activeImage.alt}
            onClick={(event) => event.stopPropagation()}
            src={activeImage.src}
          />
        </div>
      ) : null}
    </>
  );
}
