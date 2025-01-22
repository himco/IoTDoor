import gzip
import argparse

def main(input_file, output_file):
    with open(input_file, "r", encoding="utf-8") as file:
        html_content = file.read()
    compressed_data = gzip.compress(html_content.encode('utf-8'))
    compressed_length = len(compressed_data)
    c_array = ", ".join(f"0x{byte:02X}" for byte in compressed_data)
    c_code = f"""
// Create by html2bytes.py
size_t GZHTML_INDEX_LEN = {compressed_length};
const uint8_t GZHTML_INDEX[] = {{
  {c_array}
}};
"""
    with open(output_file, "w", encoding="utf-8") as file:
        file.write(c_code)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--input", default="index.html")
    parser.add_argument("-o", "--output", default="html_index.h")
    args = parser.parse_args()
    main(args.input, args.output)
