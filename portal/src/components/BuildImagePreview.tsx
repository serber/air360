/* eslint-disable @next/next/no-img-element */
"use client";

import { useState } from "react";
import { useTranslations } from "next-intl";
import Lightbox from "yet-another-react-lightbox";
import Counter from "yet-another-react-lightbox/plugins/counter";
import Zoom from "yet-another-react-lightbox/plugins/zoom";
import "yet-another-react-lightbox/styles.css";
import "yet-another-react-lightbox/plugins/counter.css";

type BuildImage = {
  alt: string;
  src: string;
};

const CLOSED = -1;

export function BuildImagePreview({
  className = "",
  images,
}: {
  className?: string;
  images: BuildImage[];
}) {
  const t = useTranslations("imagePreview");
  const [index, setIndex] = useState(CLOSED);

  return (
    <>
      <div className={`air-build-photo-grid${className ? ` ${className}` : ""}`}>
        {images.map((image, imageIndex) => (
          <button
            aria-label={image.alt}
            className="air-build-photo"
            key={image.src}
            onClick={() => setIndex(imageIndex)}
            type="button"
          >
            <img alt={image.alt} src={image.src} />
          </button>
        ))}
      </div>

      <Lightbox
        carousel={{ finite: true }}
        close={() => setIndex(CLOSED)}
        index={index}
        labels={{
          Close: t("close"),
          Next: t("next"),
          Previous: t("previous"),
          "Zoom in": t("zoomIn"),
          "Zoom out": t("zoomOut"),
        }}
        open={index !== CLOSED}
        // A counter only earns its space when there is more than one slide.
        plugins={images.length > 1 ? [Zoom, Counter] : [Zoom]}
        slides={images.map((image) => ({ alt: image.alt, src: image.src }))}
        zoom={{ maxZoomPixelRatio: 4, scrollToZoom: true }}
      />
    </>
  );
}
